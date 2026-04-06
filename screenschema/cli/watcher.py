"""
Watch mode — rebuild and reflash when the schema YAML changes (Phase 3).

Usage:
    watch("path/to/screenschema.yaml", port="/dev/ttyACM0")
"""

import pathlib
import subprocess
import time
import threading

try:
    from watchdog.observers import Observer
    from watchdog.events import FileSystemEventHandler
    _WATCHDOG = True
except ImportError:
    _WATCHDOG = False


def watch(yaml_path: str, port: str):
    """
    Watch `yaml_path` for changes and rebuild + reflash on every save.
    Blocks until Ctrl-C.
    """
    if not _WATCHDOG:
        raise ImportError("watchdog is required: pip install watchdog")

    import click
    from .schema import load_schema
    from .board_registry import BoardRegistry
    from .codegen import generate

    yaml_path = pathlib.Path(yaml_path).resolve()
    out_dir   = yaml_path.parent / "build" / "generated"
    out_dir.mkdir(parents=True, exist_ok=True)

    # Track last build time to debounce rapid saves
    _state = {"last_build": 0.0, "building": False}
    _lock  = threading.Lock()

    def rebuild():
        with _lock:
            if _state["building"]:
                return
            _state["building"] = True
        try:
            click.echo(f"\n[watch] Change detected — rebuilding...")
            schema = load_schema(yaml_path)
            board  = BoardRegistry().get(schema["board"])
            generate(schema, board, out_dir, yaml_path.parent)

            idf_sh = _find_idf_sh(out_dir)
            if idf_sh is None:
                click.echo("[watch] idf.sh not found — skipping flash")
                return

            click.echo(f"[watch] Flashing to {port}...")
            result = subprocess.run(
                [str(idf_sh), "build", "flash", "-p", port],
                cwd=str(out_dir),
            )
            if result.returncode == 0:
                click.echo("[watch] Flash complete.")
            else:
                click.echo("[watch] Flash failed — check output above.")
        except Exception as e:
            click.echo(f"[watch] Error: {e}", err=True)
        finally:
            with _lock:
                _state["building"] = False
                _state["last_build"] = time.monotonic()

    class _Handler(FileSystemEventHandler):
        def on_modified(self, event):
            if pathlib.Path(event.src_path).resolve() == yaml_path:
                # Debounce: ignore if last build was less than 2s ago
                if time.monotonic() - _state["last_build"] < 2.0:
                    return
                threading.Thread(target=rebuild, daemon=True).start()

    observer = Observer()
    observer.schedule(_Handler(), str(yaml_path.parent), recursive=False)
    observer.start()

    click.echo(f"[watch] Watching {yaml_path.name} — press Ctrl-C to stop")
    # Run an initial build
    threading.Thread(target=rebuild, daemon=True).start()

    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        observer.stop()
        observer.join()
        click.echo("[watch] Stopped.")


def _find_idf_sh(start_dir: pathlib.Path) -> pathlib.Path | None:
    current = start_dir.resolve()
    while True:
        candidate = current / "idf.sh"
        if candidate.exists():
            return candidate
        parent = current.parent
        if parent == current:
            return None
        current = parent
