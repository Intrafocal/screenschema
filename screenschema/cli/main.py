import click
import pathlib
import subprocess


@click.group()
def cli():
    """screenschema — YAML-driven UI framework for ESP32 devices."""
    pass


@cli.command()
@click.argument("yaml_path", type=click.Path(exists=True))
@click.option("--out", default=None, help="Output directory (default: build/generated/ next to yaml)")
def build(yaml_path, out):
    """Validate and generate an ESP-IDF project from a screenschema.yaml."""
    from .schema import load_schema
    from .board_registry import BoardRegistry
    from .codegen import generate

    yaml_path = pathlib.Path(yaml_path).resolve()
    schema = load_schema(yaml_path)
    board = BoardRegistry().get(schema["board"])

    if out is None:
        out_dir = yaml_path.parent / "build" / "generated"
    else:
        out_dir = pathlib.Path(out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    generate(schema, board, out_dir, yaml_path.parent)
    click.echo(f"Generated project at: {out_dir}")


@cli.command()
@click.argument("yaml_path", type=click.Path(exists=True))
@click.option("-p", "--port", required=True, help="Serial port (e.g. /dev/ttyACM0)")
@click.option("--out", default=None, help="Output directory (default: build/generated/ next to yaml)")
def flash(yaml_path, port, out):
    """Build and flash a screenschema.yaml project to a connected board."""
    from .schema import load_schema
    from .board_registry import BoardRegistry
    from .codegen import generate

    yaml_path = pathlib.Path(yaml_path).resolve()
    schema = load_schema(yaml_path)
    board = BoardRegistry().get(schema["board"])

    if out is None:
        out_dir = yaml_path.parent / "build" / "generated"
    else:
        out_dir = pathlib.Path(out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    generate(schema, board, out_dir, yaml_path.parent)
    click.echo(f"Generated project at: {out_dir}")

    # Walk up from out_dir to find idf.sh
    idf_sh = _find_idf_sh(out_dir)
    if idf_sh is None:
        raise click.ClickException(
            "Could not find idf.sh by walking up from the generated project directory. "
            "Make sure the project is inside the hardware repo tree."
        )

    click.echo(f"Flashing using: {idf_sh}")
    subprocess.run(
        [str(idf_sh), "build", "flash", "-p", port],
        cwd=str(out_dir),
        check=True,
    )


@cli.command()
@click.argument("yaml_path", type=click.Path(exists=True))
def validate(yaml_path):
    """Validate a screenschema.yaml file and print any errors."""
    from .schema import load_schema

    yaml_path = pathlib.Path(yaml_path).resolve()
    try:
        load_schema(yaml_path)
        click.echo("OK")
    except Exception as e:
        click.echo(f"ERROR: {e}", err=True)
        raise SystemExit(1)


@cli.command("boards")
def list_boards():
    """List all available board profiles."""
    from .board_registry import BoardRegistry

    reg = BoardRegistry()
    profiles = reg.all()
    if not profiles:
        click.echo("No board profiles found.")
        return
    for board_id, profile in sorted(profiles.items()):
        click.echo(f"  {board_id:30s} {profile.get('description', '')}")


@cli.command()
@click.argument("yaml_path", type=click.Path(exists=True))
@click.option("--out", default=None, help="Output directory (default: build/sim/ next to yaml)")
def sim(yaml_path, out):
    """Build and run a desktop simulator for a screenschema.yaml."""
    from .schema import load_schema
    from .board_registry import BoardRegistry
    from .codegen import generate_sim

    yaml_path = pathlib.Path(yaml_path).resolve()
    schema = load_schema(yaml_path)
    board = BoardRegistry().get(schema["board"])

    if out is None:
        out_dir = yaml_path.parent / "build" / "sim"
    else:
        out_dir = pathlib.Path(out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    generate_sim(schema, board, out_dir, yaml_path.parent)
    click.echo(f"Generated sim at: {out_dir}")

    # Build with cmake
    build_dir = out_dir / "_build"
    build_dir.mkdir(exist_ok=True)

    click.echo("Building sim...")
    subprocess.run(
        ["cmake", "-S", str(out_dir), "-B", str(build_dir)],
        check=True,
    )
    subprocess.run(
        ["cmake", "--build", str(build_dir), "--parallel"],
        check=True,
    )

    # Run
    binary = build_dir / "screenschema_sim"
    click.echo(f"Launching {binary}")
    subprocess.run([str(binary)])


@cli.command()
@click.argument("name")
def new(name):
    """Scaffold a new screenschema project directory."""
    click.echo("not yet implemented")


@cli.command()
@click.argument("yaml_path", type=click.Path(exists=True))
@click.option("-p", "--port", required=True, help="Serial port (e.g. /dev/ttyACM0)")
def watch(yaml_path, port):
    """Watch for YAML changes and auto-rebuild/flash."""
    from .watcher import watch as _watch
    _watch(yaml_path, port)


@cli.command()
@click.option("-p", "--port", required=True, help="Serial port (e.g. /dev/ttyACM0)")
@click.option("--baud", default=115200, show_default=True, help="Baud rate")
def bridge(port, baud):
    """Interactive serial bridge REPL to a connected device."""
    from .serial_bridge import DeviceBridge
    import shlex

    dev = DeviceBridge(port, baud)
    dev.connect()

    click.echo("Commands: set <id> <value>, get <id>, show <id>, hide <id>, "
               "list_add <id> <text>, list_clear <id>, ping, quit\n")

    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        if not line:
            continue
        parts = shlex.split(line)
        cmd = parts[0].lower()
        try:
            if cmd in ("quit", "exit"):
                break
            elif cmd == "set" and len(parts) >= 3:
                val_str = parts[2]
                try:
                    val = int(val_str)
                except ValueError:
                    try:
                        val = float(val_str)
                    except ValueError:
                        val = val_str
                dev.set(parts[1], val)
                click.echo("  OK")
            elif cmd == "get" and len(parts) >= 2:
                click.echo(f"  {parts[1]} = {dev.get(parts[1])!r}")
            elif cmd == "show" and len(parts) >= 2:
                dev.show(parts[1]); click.echo("  OK")
            elif cmd == "hide" and len(parts) >= 2:
                dev.hide(parts[1]); click.echo("  OK")
            elif cmd == "ping":
                click.echo("  PONG" if dev.ping() else "  timeout")
            elif cmd == "list_add" and len(parts) >= 3:
                dev.list_add(parts[1], parts[2]); click.echo("  OK")
            elif cmd == "list_clear" and len(parts) >= 2:
                dev.list_clear(parts[1]); click.echo("  OK")
            else:
                click.echo("  Unknown command.")
        except Exception as e:
            click.echo(f"  Error: {e}", err=True)

    dev.disconnect()
    click.echo("Disconnected.")


def _find_idf_sh(start_dir: pathlib.Path) -> pathlib.Path | None:
    """Walk up the directory tree from start_dir looking for idf.sh."""
    current = start_dir.resolve()
    # Prevent infinite loop — stop at filesystem root
    while True:
        candidate = current / "idf.sh"
        if candidate.exists():
            return candidate
        parent = current.parent
        if parent == current:
            # Reached filesystem root
            return None
        current = parent
