import yaml
import pathlib
from typing import Any, Dict


def load_schema(yaml_path: pathlib.Path) -> Dict[str, Any]:
    """Load and minimally validate a screenschema.yaml file."""
    with open(yaml_path) as f:
        data = yaml.safe_load(f)

    _validate(data, yaml_path)
    return data


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
