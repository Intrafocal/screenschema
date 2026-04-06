import yaml
import pathlib
from typing import Dict, Any


class UnknownBoardError(Exception):
    pass


class BoardRegistry:
    def __init__(self, boards_dir: pathlib.Path = None):
        if boards_dir is None:
            # boards/ is two levels up from cli/ (cli/../boards/)
            boards_dir = pathlib.Path(__file__).parent.parent / "boards"
        self._boards: Dict[str, Any] = {}
        if boards_dir.exists():
            for f in boards_dir.glob("*.yaml"):
                with open(f) as fp:
                    profile = yaml.safe_load(fp)
                self._boards[f.stem] = profile

    def get(self, board_id: str) -> Dict[str, Any]:
        if board_id not in self._boards:
            known = sorted(self._boards.keys())
            raise UnknownBoardError(
                f"Unknown board '{board_id}'. Known boards: {known}"
            )
        return self._boards[board_id]

    def all(self) -> Dict[str, Any]:
        return dict(self._boards)
