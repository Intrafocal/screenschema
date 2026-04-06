import yaml
import pathlib
from typing import Dict, Any, Optional


class ComponentRegistry:
    def __init__(self, project_dir: pathlib.Path = None):
        # Built-in components from screenschema/components/
        self._components: Dict[str, Any] = {}
        builtin_dir = pathlib.Path(__file__).parent.parent / "components"
        self._load_dir(builtin_dir)
        # Project-local components override builtins
        if project_dir:
            self._load_dir(project_dir / "components")

    def _load_dir(self, directory: pathlib.Path):
        if not directory.exists():
            return
        for f in directory.glob("*.yaml"):
            with open(f) as fp:
                data = yaml.safe_load(fp)
            if "component" in data:
                name = data["component"]["name"]
                self._components[name] = data["component"]

    def get(self, name: str) -> Optional[Dict[str, Any]]:
        return self._components.get(name)

    def is_component(self, type_name: str) -> bool:
        return type_name in self._components
