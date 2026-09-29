"""Logins: local OpenAI-compatible servers, API-key providers and the ChatGPT (Codex) subscription.

`logins.toml` (written by /model and /login) lists them and names the active login and model.
Keys pasted into /login are saved as `keys/<login>` (0600). HEPH_BASE_URL, HEPH_API_KEY and
HEPH_MODEL override everything, for scripts.
"""

import json
import os
import re
from dataclasses import dataclass, replace
from pathlib import Path
from urllib.parse import urlsplit

from heph import HephError, codex
from heph.config import config_dir, read_toml
from heph.llm import Client, ModelClient

DEFAULT_URL = "http://127.0.0.1:8080/v1"
CODEX = "codex"
_ENV = "HEPH_BASE_URL"
_NAME = re.compile(r"[A-Za-z0-9._:@-]+")


@dataclass(frozen=True, slots=True)
class Provider:
    title: str
    base_url: str  # empty: the user gives one
    key_env: str  # read when the login has no saved key


PROVIDERS: dict[str, Provider] = {
    "local": Provider("local or self-hosted OpenAI-compatible server", "", ""),
    "openai": Provider("OpenAI API", "https://api.openai.com/v1", "OPENAI_API_KEY"),
    "openrouter": Provider("OpenRouter", "https://openrouter.ai/api/v1", "OPENROUTER_API_KEY"),
    "deepseek": Provider("DeepSeek", "https://api.deepseek.com", "DEEPSEEK_API_KEY"),
    "zai": Provider("Z.AI", "https://api.z.ai/api/paas/v4", "ZAI_API_KEY"),
    CODEX: Provider("ChatGPT subscription (Codex)", "", ""),
}


@dataclass(frozen=True, slots=True)
class Login:
    name: str
    provider: str
    base_url: str
    key_file: str  # a key file you keep elsewhere; empty: keys/<name> if Heph saved one


@dataclass(frozen=True, slots=True)
class Logins:
    path: Path
    items: tuple[Login, ...]
    login: str  # active login name; empty: the default local server
    model: str  # active model; empty: the first one the login lists

    def get(self, name: str) -> Login | None:
        return next((item for item in self.items if item.name == name), None)


def key_path(name: str) -> Path:
    if not _NAME.fullmatch(name):
        raise HephError(f"Invalid login name {name!r}")
    return config_dir() / "keys" / name


def write_private(path: Path, text: str) -> None:
    """Writes a file only its owner can read, atomically, never through a symlink."""
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    tmp = path.with_name(f".{path.name}.tmp")
    tmp.unlink(missing_ok=True)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW
    with os.fdopen(os.open(tmp, flags, 0o600), "w", encoding="utf-8") as file:
        file.write(text)
    tmp.replace(path)


def url_login(url: str) -> Login:
    """A local server login named after its host and port."""
    parts = urlsplit(url.strip())
    if parts.scheme not in {"http", "https"} or not parts.netloc:
        raise HephError(f"Not a server URL: {url!r} (like http://127.0.0.1:8080/v1)")
    return Login(parts.netloc, "local", url.strip().rstrip("/"), "")


def _login(name: str, raw: object, path: Path) -> Login:
    match raw:
        case {"provider": str(provider), **rest} if provider in PROVIDERS:
            base_url, key_file = rest.get("base_url", ""), rest.get("key_file", "")
            unknown = sorted(set(rest) - {"base_url", "key_file"})
        case _:
            raise HephError(f"Login {name!r} in {path} needs a known provider")
    if unknown or not isinstance(base_url, str) or not isinstance(key_file, str):
        raise HephError(f"Login {name!r} in {path} has bad fields")
    if provider != CODEX and not base_url.startswith(("http://", "https://")):
        raise HephError(f"Login {name!r} in {path} needs an http(s) base_url")
    _ = key_path(name)
    return Login(name, provider, base_url.rstrip("/"), key_file)


def load() -> Logins:
    path = config_dir() / "logins.toml"
    data = read_toml(path, frozenset({"login", "model", "logins"}))
    login, model, table = data.get("login", ""), data.get("model", ""), data.get("logins", {})
    if not isinstance(login, str) or not isinstance(model, str) or not isinstance(table, dict):
        raise HephError(f"{path} must hold login and model strings and a [logins] table")
    items = tuple(_login(str(name), raw, path) for name, raw in table.items())
    return Logins(path, items, login, model)


def _quote(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)  # a valid TOML basic string


def save(logins: Logins) -> None:
    lines = [f"login = {_quote(logins.login)}", f"model = {_quote(logins.model)}"]
    for item in logins.items:
        lines += ["", f"[logins.{_quote(item.name)}]", f"provider = {_quote(item.provider)}"]
        if item.base_url:
            lines.append(f"base_url = {_quote(item.base_url)}")
        if item.key_file:
            lines.append(f"key_file = {_quote(item.key_file)}")
    write_private(logins.path, "\n".join(lines) + "\n")


def add(logins: Logins, login: Login) -> Logins:
    """Adds or replaces a login by name."""
    items = (*(item for item in logins.items if item.name != login.name), login)
    return replace(logins, items=items)


def remove(logins: Logins, name: str) -> Logins:
    """Drops a login and the key Heph saved for it (never a key file you keep elsewhere)."""
    key_path(name).unlink(missing_ok=True)
    active = "" if logins.login == name else logins.login
    items = tuple(item for item in logins.items if item.name != name)
    return replace(logins, items=items, login=active, model=logins.model if active else "")


def active(logins: Logins) -> tuple[Login, str]:
    """The login and model to use: HEPH_BASE_URL, else the saved choice, else the default."""
    model = os.environ.get("HEPH_MODEL", "")
    if url := os.environ.get(_ENV, ""):
        return replace(url_login(url), name=_ENV), model
    if (login := logins.get(logins.login)) is not None:
        return login, model or logins.model
    return url_login(DEFAULT_URL), model


def api_key(login: Login) -> str:
    """The login's key: its key file, else the key Heph saved, else the provider's env var."""
    if login.name == _ENV:
        return os.environ.get("HEPH_API_KEY", "")
    own = key_path(login.name)
    file = Path(login.key_file).expanduser() if login.key_file else own
    if file == own and not own.exists():
        env = PROVIDERS[login.provider].key_env
        return os.environ.get(env, "") if env else ""
    try:
        key = file.read_text(encoding="utf-8").strip()
    except OSError as exc:
        raise HephError(f"Cannot read the key for {login.name} ({file}): {exc.strerror}") from exc
    if not key:
        raise HephError(f"The key file for {login.name} is empty: {file}")
    return key


def client(login: Login) -> ModelClient:
    if login.provider == CODEX:
        return codex.CodexClient(key_path(login.name))
    return Client(login.base_url, api_key(login))
