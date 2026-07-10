import os
import yaml
import pathlib
from typing import Any, Dict

_WIDGET_HANDLER_KEYS = ("on_click", "on_long_press", "on_change",
                        "on_release", "on_submit", "on_select")
_APP_HANDLER_KEYS = ("on_init", "on_resume", "on_pause", "on_close")


def load_schema(yaml_path: pathlib.Path) -> Dict[str, Any]:
    """Load and minimally validate a screenschema.yaml file."""
    with open(yaml_path) as f:
        data = yaml.safe_load(f)

    _expand_packages(data, yaml_path.parent)
    _validate(data, yaml_path)
    return data


def _expand_packages(data: dict, yaml_dir: pathlib.Path):
    """Replace {package: <dir>} entries in apps: with the package's app.yaml.

    Annotates each expanded app with:
      _package_dir      (str, absolute)
      _package_handlers (str, absolute path to the handlers file, or None)
    and merges the package's build.extra_component_dirs / main_requires into
    the device schema's build: section (component dirs as absolute paths).
    """
    apps = data.get("apps") if isinstance(data, dict) else None
    if not isinstance(apps, list):
        return
    expanded = []
    for entry in apps:
        if not (isinstance(entry, dict) and "package" in entry):
            expanded.append(entry)
            continue
        extra = set(entry) - {"package"}
        if extra:
            raise ValueError(
                f"package app entry has unsupported keys: {sorted(extra)}")
        pkg_dir = pathlib.Path(entry["package"])
        if not pkg_dir.is_absolute():
            pkg_dir = (yaml_dir / pkg_dir).resolve()
        manifest = pkg_dir / "app.yaml"
        if not manifest.is_file():
            raise ValueError(f"app package '{entry['package']}': missing {manifest}")
        with open(manifest) as f:
            app = yaml.safe_load(f) or {}
        if not isinstance(app, dict) or "id" not in app:
            raise ValueError(f"{manifest}: package app.yaml must define 'id'")

        pkg_build     = app.pop("build", {}) or {}
        handlers_rel  = app.pop("handlers", "handlers.cpp")
        handlers_path = pkg_dir / handlers_rel
        if _app_handler_names(app) and not handlers_path.is_file():
            raise ValueError(f"{manifest}: handlers file not found: {handlers_path}")

        if app.get("icon") and not os.path.isabs(app["icon"]):
            app["icon"] = str((pkg_dir / app["icon"]).resolve())

        app["_package_dir"]      = str(pkg_dir)
        app["_package_handlers"] = str(handlers_path) if handlers_path.is_file() else None

        dev_build = data.setdefault("build", {})
        for p in pkg_build.get("extra_component_dirs", []):
            abs_p = p if os.path.isabs(p) else str((pkg_dir / p).resolve())
            dirs = dev_build.setdefault("extra_component_dirs", [])
            if abs_p not in dirs:
                dirs.append(abs_p)
        for r in pkg_build.get("main_requires", []):
            reqs = dev_build.setdefault("main_requires", [])
            if r not in reqs:
                reqs.append(r)
        expanded.append(app)
    data["apps"] = expanded


def _app_handler_names(app: dict) -> set:
    """All handler function names declared by an app (widget on_* + lifecycle)."""
    names = set()
    for w in app.get("widgets", []) or []:
        for key in _WIDGET_HANDLER_KEYS:
            if w.get(key):
                names.add(w[key])
    for key in _APP_HANDLER_KEYS:
        if app.get(key):
            names.add(app[key])
    return names


def _validate(data: dict, path: pathlib.Path):
    if "schema_version" not in data:
        raise ValueError(f"{path}: missing 'schema_version'")
    if "board" not in data:
        raise ValueError(f"{path}: missing 'board'")
    if "apps" not in data or not data["apps"]:
        raise ValueError(f"{path}: 'apps' must be a non-empty list")

    seen_ids = set()
    for app in data["apps"]:
        if "id" not in app:
            raise ValueError(f"{path}: app missing 'id'")
        for widget in app.get("widgets", []):
            if "id" not in widget:
                raise ValueError(f"{path}: widget missing 'id' in app '{app['id']}'")
            wid = widget["id"]
            if wid in seen_ids:
                raise ValueError(f"{path}: duplicate widget id '{wid}'")
            seen_ids.add(wid)

    # Duplicate app ids — required so handlers_<id>.cpp / class names are unique
    seen_apps = set()
    for app in data["apps"]:
        if app["id"] in seen_apps:
            raise ValueError(f"{path}: duplicate app id '{app['id']}'")
        seen_apps.add(app["id"])

    # Derived class-name collision — e.g. ids 'foo' and 'foo_app' both generate
    # class FooApp. Caught here so `validate` fails cleanly; codegen's
    # _build_context keeps the same check as a backstop for direct callers.
    class_names = {}
    for app in data["apps"]:
        pascal = "".join(w.capitalize() for w in app["id"].split("_"))
        cls = pascal if pascal.endswith("App") else pascal + "App"
        if cls in class_names:
            raise ValueError(
                f"{path}: apps '{class_names[cls]}' and '{app['id']}' both "
                f"generate class {cls} — rename one")
        class_names[cls] = app["id"]

    # Handler symbols: two PACKAGES both defining the same function is a
    # guaranteed duplicate-symbol link error — fail at validate time.
    pkg_owner = {}   # handler name -> owning package app id
    for app in data["apps"]:
        if not app.get("_package_handlers"):
            continue
        for h in _app_handler_names(app):
            if h in pkg_owner and pkg_owner[h] != app["id"]:
                raise ValueError(
                    f"{path}: handler '{h}' defined by both package apps "
                    f"'{pkg_owner[h]}' and '{app['id']}' (duplicate symbol)")
            pkg_owner[h] = app["id"]
