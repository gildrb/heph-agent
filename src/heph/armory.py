"""Armory layout, init/validate/resolve, material discovery, safe reads and adding files.

An armory is any folder with a `.harness/armory.toml` marker: every visible file below it is
material, except `.harness/` itself and what `.harnessignore` excludes.
"""

import fnmatch
import os
import shutil
import stat
import tempfile
from datetime import UTC, datetime
from pathlib import Path
from typing import NoReturn

from heph import HephError

INTERNAL = ".harness"
MARKER = f"{INTERNAL}/armory.toml"
INDEX = f"{INTERNAL}/index"
CHATS = f"{INTERNAL}/chats"
IGNORE_FILE = ".harnessignore"
DEFAULT_IGNORE = (".git/", "__pycache__/", "node_modules/", ".DS_Store")
_DIRS = (INTERNAL, INDEX, CHATS)
_DIR_FLAGS = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW


def armory_home() -> Path:
    return Path(os.environ.get("HEPH_ARMORY_HOME") or Path.home() / ".armories")


def _target(name_or_path: str | None) -> Path:
    if name_or_path is None:
        return Path.cwd()
    if os.sep in name_or_path or name_or_path.startswith((".", "~")):
        return Path(name_or_path).expanduser().absolute()
    return armory_home() / name_or_path


def init(name_or_path: str | None) -> Path:
    """Makes a folder an armory: None is the current folder, a bare name lives in the home."""
    root = _target(name_or_path)
    resolved = root.resolve()
    if resolved in {Path.home().resolve(), Path(resolved.anchor)}:
        raise HephError(f"Refusing to make {root} an armory; use a folder of documents inside it")
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


def here() -> Path | None:
    """The current folder if it is an armory."""
    cwd = Path.cwd()
    return validate(cwd) if (cwd / MARKER).is_file() else None


def known() -> list[Path]:
    """Armories in the armory home, sorted by name."""
    home = armory_home()
    found = home.iterdir() if home.is_dir() else ()
    return sorted((p for p in found if (p / MARKER).is_file()), key=lambda p: p.name)


def resolve(arg: str | None) -> Path:
    """Resolves an armory argument: the cwd, a path (has a `/` or starts with `.`/`~`), or a
    name under the armory home; a bare name falls back to a path only if no such armory exists.
    """
    if arg is None:
        if (cwd := here()) is not None:
            return cwd
        names = ", ".join(p.name for p in known())
        hint = f" Or name one: {names}." if names else ""
        raise HephError(f"{Path.cwd()} is not an armory. Run `heph init` to make it one.{hint}")
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
    """Visible files below the armory root as sorted armory-relative POSIX paths."""
    patterns = _patterns(root)
    found: list[str] = []
    for current, dirs, files in os.walk(root, onerror=_raise):
        folder = Path(current)
        rel = folder.relative_to(root).parts
        dirs[:] = sorted(
            name
            for name in dirs
            if not name.startswith(".")
            and not (folder / name).is_symlink()
            and not _ignored((*rel, name), patterns, is_dir=True)
        )
        for name in files:
            path = folder / name
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


def add(root: Path, source: Path) -> str:
    """Copies a file or folder into the armory root; never overwrites, all or nothing."""
    if not source.exists():
        raise HephError(f"No such file or folder: {source}")
    resolved = source.resolve()
    if resolved.is_relative_to(root):
        raise HephError(f"{source} is already in the armory")
    if root.is_relative_to(resolved):
        raise HephError(f"{source} contains the armory")
    if not (resolved.is_dir() or resolved.is_file()):
        raise HephError(f"{source} is not a regular file or folder")
    target = root / resolved.name
    if target.exists() or target.is_symlink():
        raise HephError(f"{target.name} already exists in the armory")
    staging = Path(tempfile.mkdtemp(prefix="add-", dir=root / INTERNAL))
    try:
        copy = staging / resolved.name
        if resolved.is_dir():
            shutil.copytree(resolved, copy, symlinks=True)
        else:
            shutil.copy2(resolved, copy)
        copy.rename(target)
    except OSError as exc:
        raise HephError(f"Cannot copy {source}: {exc}") from exc
    finally:
        shutil.rmtree(staging, ignore_errors=True)
    return target.name
