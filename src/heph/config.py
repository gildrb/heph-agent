"""Settings from `~/.config/heph/config.toml` with `HEPH_*` environment overrides."""

import os
import tomllib
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path

from heph import HephError

DEFAULT_BASE_URL = "http://127.0.0.1:8080/v1"
_KEYS = frozenset(
    {"base_url", "model", "api_key_env", "max_tokens", "temperature", "evidence_tokens"}
)


@dataclass(frozen=True, slots=True)
class Config:
    path: Path
    base_url: str
    model: str
    api_key: str
    api_key_env: str
    max_tokens: int
    temperature: float
    evidence_tokens: int


def config_path() -> Path:
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / "heph" / "config.toml"


def _read(path: Path) -> Mapping[str, object]:
    if not path.exists():
        return {}
    try:
        with path.open("rb") as file:
            data = tomllib.load(file)
    except tomllib.TOMLDecodeError as exc:
        raise HephError(f"Invalid TOML in {path}: {exc}") from exc
    unknown = sorted(set(data) - _KEYS)
    if unknown:
        raise HephError(f"Unknown key(s) in {path}: {', '.join(unknown)}")
    return data


def _str(data: Mapping[str, object], key: str, default: str, path: Path) -> str:
    value = data.get(key, default)
    if not isinstance(value, str):
        raise HephError(f"{key} in {path} must be a string")
    return value


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
    path = config_path()
    data = _read(path)
    api_key_env = _str(data, "api_key_env", "", path)
    api_key = os.environ.get("HEPH_API_KEY", "")
    if not api_key and api_key_env:
        api_key = os.environ.get(api_key_env, "")
        if not api_key:
            raise HephError(f"api_key_env in {path} names {api_key_env}, which is not set")
    base_url = os.environ.get("HEPH_BASE_URL") or _str(data, "base_url", DEFAULT_BASE_URL, path)
    if not base_url.startswith(("http://", "https://")):
        raise HephError(f"base_url must start with http:// or https:// (got {base_url!r})")
    return Config(
        path=path,
        base_url=base_url.rstrip("/"),
        model=os.environ.get("HEPH_MODEL") or _str(data, "model", "", path),
        api_key=api_key,
        api_key_env=api_key_env,
        max_tokens=_int(data, "max_tokens", 2048, path),
        temperature=_float(data, "temperature", 0.2, path),
        evidence_tokens=_int(data, "evidence_tokens", 3000, path),
    )
