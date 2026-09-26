"""Armory layout, init/validate/resolve, material discovery and safe material reads."""

import fnmatch
import os
import stat
from datetime import UTC, datetime
from pathlib import Path
from typing import NoReturn

from heph import HephError

MATERIALS = "materials"
INTERNAL = ".harness"
MARKER = f"{INTERNAL}/armory.toml"
INDEX = f"{INTERNAL}/index"
CHATS = f"{INTERNAL}/chats"
IGNORE_FILE = ".harnessignore"
DEFAULT_IGNORE = (".git/", "__pycache__/", ".DS_Store")
_DIRS = (MATERIALS, INTERNAL, INDEX, CHATS)
_DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW


def armory_home() -> Path:
    return Path(os.environ.get("HEPH_ARMORY_HOME") or Path.home() / ".armories")


def _target(name_or_path: str) -> Path:
    if os.sep in name_or_path or name_or_path.startswith((".", "~")):
        return Path(name_or_path).expanduser().absolute()
    return armory_home() / name_or_path


def init(name_or_path: str) -> Path:
    root = _target(name_or_path)
    if (root / MARKER).exists():
        raise HephError(f"{root} is already an armory")
    if root.exists() and not root.is_dir():
        raise HephError(f"{root} exists and is not a directory")
    for rel in _DIRS:
        (root / rel).mkdir(parents=True, exist_ok=True)
    created = datetime.now(UTC).isoformat(timespec="seconds")
    (root / MARKER).write_text(f'version = 1\ncreated_at = "{created}"\n', encoding="utf-8")
    return validate(root)


def validate(root: Path) -> Path:
    """Checks the layout and returns the resolved armory root."""
    if not root.is_dir():
        raise HephError(f"No armory at {root}. Create one with `heph init {root}`.")
    resolved = root.resolve()
    if not (resolved / MARKER).is_file():
        raise HephError(f"{root} is not an armory (missing {MARKER}); run `heph init {root}`")
    for rel in _DIRS:
        path = resolved / rel
        if path.is_symlink():
            raise HephError(f"Refusing symlinked armory directory {path}")
        path.mkdir(exist_ok=True)
        if not path.resolve().is_relative_to(resolved):
            raise HephError(f"Armory directory {path} escapes the armory")
    return resolved


def resolve(arg: str | None) -> Path:
    """Resolves an armory argument: the cwd, a path (has a `/` or starts with `.`/`~`), or a
    name under the armory home; a bare name falls back to a path only if no such armory exists.
    """
    if arg is None:
        cwd = Path.cwd()
        if (cwd / MARKER).is_file():
            return validate(cwd)
        home = armory_home()
        found = home.iterdir() if home.is_dir() else ()
        names = sorted(p.name for p in found if (p / MARKER).is_file())
        known = f" Known armories in {home}: {', '.join(names)}." if names else ""
        raise HephError(f"No armory given and {cwd} is not an armory.{known}")
    path = Path(arg).expanduser()
    named = armory_home() / arg
    is_path = os.sep in arg or arg.startswith((".", "~"))
    if is_path or not (named / MARKER).is_file():
        return validate(path if path.exists() else named)
    return validate(named)


def _patterns(root: Path) -> list[str]:
    patterns: list[str] = list(DEFAULT_IGNORE)
    ignore = root / IGNORE_FILE
    if ignore.is_file() and not ignore.is_symlink():
        lines = ignore.read_text(encoding="utf-8").splitlines()
        patterns += [s for line in lines if (s := line.strip()) and not s.startswith("#")]
    return patterns


def _ignored(parts: tuple[str, ...], patterns: list[str], *, is_dir: bool) -> bool:
    """gitignore-style subset: `dir/`, globs, anchored when the pattern has an inner `/`."""
    for pattern in patterns:
        dir_only = pattern.endswith("/")
        if dir_only and not is_dir:
            continue
        body = pattern.strip("/")
        if "/" in body:
            if fnmatch.fnmatchcase("/".join(parts), body):
                return True
        elif fnmatch.fnmatchcase(parts[-1], body):
            return True
    return False


def _raise(exc: OSError) -> NoReturn:
    raise HephError(f"Cannot list materials: {exc}") from exc


def materials(root: Path) -> list[str]:
    """Visible material files as sorted armory-relative POSIX paths (`materials/...`)."""
    patterns = _patterns(root)
    base = root / MATERIALS
    found: list[str] = []
    for current, dirs, files in os.walk(base, onerror=_raise):
        here = Path(current)
        rel = here.relative_to(root).parts
        dirs[:] = sorted(
            name
            for name in dirs
            if not name.startswith(".")
            and not (here / name).is_symlink()
            and not _ignored((*rel, name), patterns, is_dir=True)
        )
        for name in files:
            path = here / name
            if name.startswith(".") or _ignored((*rel, name), patterns, is_dir=False):
                continue
            if stat.S_ISREG(path.lstat().st_mode):
                found.append("/".join((*rel, name)))
    return sorted(found)


def read_material(root: Path, rel: str, limit: int) -> bytes:
    """Reads a material without following symlinks anywhere below the armory root."""
    parts = rel.split("/")
    fd = os.open(root, _DIR_FLAGS)
    try:
        for part in parts[:-1]:
            child = os.open(part, _DIR_FLAGS, dir_fd=fd)
            os.close(fd)
            fd = child
        file_fd = os.open(parts[-1], os.O_RDONLY | os.O_NOFOLLOW, dir_fd=fd)
    finally:
        os.close(fd)
    with os.fdopen(file_fd, "rb") as file:
        info = os.fstat(file.fileno())
        if not stat.S_ISREG(info.st_mode):
            raise HephError(f"{rel} is not a regular file")
        if info.st_size > limit:
            raise HephError(f"{rel} exceeds the {limit // 2**20} MB size limit")
        data = file.read(limit + 1)
    if len(data) > limit:
        raise HephError(f"{rel} exceeds the {limit // 2**20} MB size limit")
    return data
