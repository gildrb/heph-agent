"""Settings from `~/.config/heph/config.toml`: answer length, sampling and evidence budget.

Which model to talk to is not here: `/model` and `/login` keep that in `logins.toml`.
"""

import os
import tomllib
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path

from heph import HephError

_KEYS = frozenset({"max_tokens", "temperature", "evidence_tokens"})


@dataclass(frozen=True, slots=True)
class Config:
    path: Path
    max_tokens: int
    temperature: float
    evidence_tokens: int


def config_dir() -> Path:
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / "heph"


def read_toml(path: Path, keys: frozenset[str]) -> Mapping[str, object]:
    """A TOML file as a mapping; a missing file is empty; unknown top-level keys are errors."""
    if not path.exists():
        return {}
    try:
        with path.open("rb") as file:
            data = tomllib.load(file)
    except tomllib.TOMLDecodeError as exc:
        raise HephError(f"Invalid TOML in {path}: {exc}") from exc
    unknown = sorted(set(data) - keys)
    if unknown:
        raise HephError(f"Unknown key(s) in {path}: {', '.join(unknown)}")
    return data


def _int(data: Mapping[str, object], key: str, default: int, path: Path) -> int:
    value = data.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise HephError(f"{key} in {path} must be a positive integer")
    return value


def _float(data: Mapping[str, object], key: str, default: float, path: Path) -> float:
    value = data.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int | float) or value < 0:
        raise HephError(f"{key} in {path} must be a non-negative number")
    return float(value)


def load() -> Config:
    path = config_dir() / "config.toml"
    data = read_toml(path, _KEYS)
    return Config(
        path=path,
        max_tokens=_int(data, "max_tokens", 8192, path),
        temperature=_float(data, "temperature", 0.2, path),
        evidence_tokens=_int(data, "evidence_tokens", 3000, path),
    )
