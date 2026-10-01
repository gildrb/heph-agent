"""ChatGPT/Codex subscription: PKCE OAuth login and the Codex Responses backend."""

import base64
import errno
import hashlib
import http.client
import json
import os
import secrets
import ssl
import threading
import time
from collections.abc import Callable, Iterator
from dataclasses import asdict, dataclass, field
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlencode, urlsplit

import certifi

from heph import HephError, reasoning
from heph.llm import Delta, Finish, Message, Usage
from heph.reasoning import Dial, Setting

# The models openai/codex lists for ChatGPT logins, in its order (models.json at b1e72963,
# 2026-09-29); their reasoning levels are in heph.reasoning.
MODELS: tuple[str, ...] = (
    "gpt-6.1-sol",
    "gpt-6-astra",
    "gpt-6-sol",
    "gpt-6-luna",
    "gpt-5.6-sol",
    "gpt-5.6-terra",
    "gpt-5.6-luna",
    "gpt-5.5",
)

_CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann"
_AUTHORIZE_URL = "https://auth.openai.com/oauth/authorize"
_TOKEN_URL = "https://auth.openai.com/oauth/token"
_RESPONSES_URL = "https://chatgpt.com/backend-api/codex/responses"
_REDIRECT_URI = "http://localhost:1455/auth/callback"
_PORT = 1455
_ORIGINATOR = "harness"  # what old Heph sent; kept so the login flow matches a known-good one
_CALLBACK_TIMEOUT = 300.0
_RELOGIN = "Run `/login codex`."


def _at(value: object, *path: str) -> object:
    for key in path:
        match value:
            case {**fields}:
                value = fields.get(key)
            case _:
                return None
    return value


@dataclass(frozen=True, slots=True)
class _Tokens:
    access: str
    refresh: str
    expires: int  # epoch seconds
    account_id: str


@dataclass(slots=True)
class _Hit:
    """The login callback listener's shared state: written by its thread, read after `done`."""

    deadline: float
    done: threading.Event = field(default_factory=threading.Event)
    stop: threading.Event = field(default_factory=threading.Event)
    closed: threading.Event = field(default_factory=threading.Event)
    code: str = ""
    error: str = ""


def _post(url: str, body: bytes, headers: dict[str, str]) -> http.client.HTTPResponse:
    parts = urlsplit(url)
    context = ssl.create_default_context(cafile=certifi.where())
    conn = http.client.HTTPSConnection(
        parts.hostname or "", parts.port, timeout=10, context=context
    )
    try:
        conn.request("POST", parts.path, body=body, headers=headers)
        if conn.sock is not None:
            conn.sock.settimeout(300.0)
        response = conn.getresponse()
    except (OSError, http.client.HTTPException) as exc:
        raise HephError(f"POST {url} failed: {exc}") from exc
    if response.status >= HTTPStatus.BAD_REQUEST:
        snippet = " ".join(response.read(800).decode(errors="replace").split())
        response.close()
        hint = f" {_RELOGIN}" if response.status in {401, 403} else ""
        raise HephError(f"POST {url} returned HTTP {response.status}: {snippet}{hint}")
    return response


def _account_id(access: str) -> str:
    payload = [*access.split("."), ""][1]
    try:
        claims: object = json.loads(base64.urlsafe_b64decode(payload + "=" * (-len(payload) % 4)))
    except ValueError as exc:
        raise HephError(f"Codex access token is not a readable JWT: {exc}") from exc
    match _at(claims, "https://api.openai.com/auth", "chatgpt_account_id"):
        case str(account) if account:
            return account
        case _:
            raise HephError("Codex access token carries no ChatGPT account id.")


def _grant(body: bytes, content_type: str) -> _Tokens:
    headers = {"Content-Type": content_type, "Accept": "application/json"}
    with _post(_TOKEN_URL, body, headers) as response:
        try:
            data: object = json.loads(response.read())
        except (http.client.HTTPException, OSError, ValueError) as exc:
            raise HephError(f"Malformed token response: {exc}") from exc
    match data:
        case {"access_token": str(access), "refresh_token": str(refresh), "expires_in": int(ttl)}:
            return _Tokens(access, refresh, int(time.time()) + ttl, _account_id(access))
        case _:
            raise HephError("Token response lacks access_token, refresh_token or expires_in.")


def _save(path: Path, tokens: _Tokens) -> None:
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    path.parent.chmod(0o700)
    tmp = path.with_name(f".{path.name}.{secrets.token_hex(6)}.tmp")
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as file:
            json.dump(asdict(tokens), file)
            file.flush()
            os.fsync(file.fileno())
        tmp.replace(path)
    except BaseException:
        tmp.unlink(missing_ok=True)
        raise


def _load(path: Path) -> _Tokens:
    try:
        with os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW), encoding="utf-8") as file:
            data: object = json.load(file)
    except FileNotFoundError as exc:
        raise HephError(f"Not logged in to Codex. {_RELOGIN}") from exc
    except (OSError, ValueError) as exc:
        raise HephError(f"Cannot read Codex login {path}: {exc}. {_RELOGIN}") from exc
    match data:
        case {"access": str(a), "refresh": str(r), "expires": int(e), "account_id": str(i)}:
            return _Tokens(a, r, e, i)
        case _:
            raise HephError(f"Codex login {path} is incomplete. {_RELOGIN}")


def _callback_code(query: str, state: str) -> str:
    params = parse_qs(query)
    if (error := params.get("error")) is not None:
        raise HephError(f"auth.openai.com refused the login: {error[0][:200]}")
    if params.get("state") != [state]:
        raise HephError("OAuth state mismatch; start `/login codex` again.")
    match params.get("code"):
        case [str(code)]:
            return code
        case _:
            raise HephError("The redirect carried no authorization code.")


def _listen(state: str) -> _Hit | None:
    """Serves one /auth/callback hit on 127.0.0.1:1455 in a thread; None if the port is busy."""
    hit = _Hit(deadline=time.monotonic() + _CALLBACK_TIMEOUT)

    class Callback(BaseHTTPRequestHandler):
        timeout = 10.0

        def do_GET(self) -> None:
            parts = urlsplit(self.path)
            if parts.path != "/auth/callback":
                self.send_error(HTTPStatus.NOT_FOUND)
                return
            try:
                hit.code = _callback_code(parts.query, state)
                status, text = HTTPStatus.OK, "Heph login received. You can close this tab."
            except HephError as exc:
                hit.error = str(exc)
                status, text = HTTPStatus.BAD_REQUEST, hit.error
            self.send_response(status)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            self.wfile.write(text.encode())
            hit.done.set()

        def log_message(self, format: str, *args: object) -> None:  # noqa: A002
            del format, args  # silent: request lines carry the authorization code

    try:
        server = HTTPServer(("127.0.0.1", _PORT), Callback)
    except OSError as exc:
        if exc.errno == errno.EADDRINUSE:
            return None
        raise HephError(f"Cannot listen on 127.0.0.1:{_PORT}: {exc}") from exc
    server.timeout = 0.25

    def serve() -> None:
        try:
            with server:
                while (
                    not (hit.done.is_set() or hit.stop.is_set())
                    and time.monotonic() < hit.deadline
                ):
                    server.handle_request()
        finally:
            hit.closed.set()

    threading.Thread(target=serve, name="codex-login", daemon=True).start()
    return hit


def _code(answer: str, state: str, hit: _Hit | None) -> str:
    if "://" in answer:
        return _callback_code(urlsplit(answer).query, state)
    if "=" in answer:
        return _callback_code(answer.lstrip("?"), state)
    if answer:
        return answer
    if hit is None:
        raise HephError("No redirect URL or code was pasted.")
    if not hit.done.wait(max(0.0, hit.deadline - time.monotonic())):
        raise HephError(f"Timed out waiting for the browser on localhost:{_PORT}.")
    if hit.error:
        raise HephError(hit.error)
    return hit.code


def login(tokens: Path, tell: Callable[[str], None], ask: Callable[[str], str]) -> None:
    """Signs in with a ChatGPT subscription; writes the tokens to `tokens` (0600)."""
    verifier = secrets.token_urlsafe(64)
    digest = hashlib.sha256(verifier.encode()).digest()
    challenge = base64.urlsafe_b64encode(digest).rstrip(b"=").decode()
    state = secrets.token_urlsafe(24)
    query = urlencode(
        {
            "response_type": "code",
            "client_id": _CLIENT_ID,
            "redirect_uri": _REDIRECT_URI,
            "scope": "openid profile email offline_access",
            "code_challenge": challenge,
            "code_challenge_method": "S256",
            "state": state,
            "id_token_add_organizations": "true",
            "codex_cli_simplified_flow": "true",
            "originator": _ORIGINATOR,
        }
    )
    hit = _listen(state)
    try:
        then = (
            "When it shows success, press Enter. If the browser runs on another machine,"
            if hit is not None
            else f"Port {_PORT} is busy, so"
        )
        tell(
            f"Open this URL in a browser and sign in with ChatGPT:\n\n  {_AUTHORIZE_URL}?{query}"
            f"\n\n{then} paste the address it lands on ({_REDIRECT_URI}?code=...) or the code."
        )
        code = _code(ask("Redirect URL or code: ").strip(), state, hit)
    finally:
        if hit is not None:
            hit.stop.set()
            hit.closed.wait()
    form = {
        "grant_type": "authorization_code",
        "client_id": _CLIENT_ID,
        "code": code,
        "code_verifier": verifier,
        "redirect_uri": _REDIRECT_URI,
    }
    _save(tokens, _grant(urlencode(form).encode(), "application/x-www-form-urlencoded"))


@dataclass(frozen=True, slots=True)
class CodexClient:
    tokens: Path

    def models(self) -> list[str]:
        return list(MODELS)

    def dial(self, model: str) -> Dial | None:
        return reasoning.dial("codex", model)

    def _auth(self) -> _Tokens:
        auth = _load(self.tokens)
        if auth.expires - 60 > time.time():
            return auth
        body = {
            "grant_type": "refresh_token",
            "refresh_token": auth.refresh,
            "client_id": _CLIENT_ID,
        }
        try:
            auth = _grant(json.dumps(body).encode(), "application/json")
        except HephError as exc:
            raise HephError(f"Codex login expired and refresh failed ({exc}). {_RELOGIN}") from exc
        _save(self.tokens, auth)
        return auth

    def stream(
        self,
        model: str,
        messages: list[Message],
        max_tokens: int,
        temperature: float,
        setting: Setting | None,
    ) -> Iterator[Delta | Usage | Finish]:
        """Yields reasoning/text deltas, the finish reason and usage.

        The Codex backend rejects max_output_tokens and temperature; neither is sent.
        """
        del max_tokens, temperature
        instructions: list[str] = []
        inputs: list[object] = []
        for message in messages:
            match message.get("role"), message.get("content"):
                case "system", str(text):
                    instructions.append(text)
                case ("user" | "assistant") as role, str(text):
                    kind = "input_text" if role == "user" else "output_text"
                    inputs.append({"role": role, "content": [{"type": kind, "text": text}]})
                case role, _:
                    raise HephError(f"Codex cannot send a message with role {role!r}.")
        body = {
            "model": model,
            "instructions": "\n\n".join(instructions),
            "input": inputs,
            "store": False,
            "stream": True,
            "reasoning": reasoning.fields(setting.dial, setting.level)["reasoning"]
            if setting is not None
            else {"summary": "auto"},
        }
        auth = self._auth()
        headers = {
            "Authorization": f"Bearer {auth.access}",
            "ChatGPT-Account-ID": auth.account_id,
            "User-Agent": "harness-cli",
            "Content-Type": "application/json",
            "Accept": "text/event-stream",
        }
        with _post(_RESPONSES_URL, json.dumps(body).encode(), headers) as response:
            try:
                yield from _events(response)
            except (http.client.HTTPException, OSError) as exc:
                raise HephError(f"Codex stream broke off: {exc}") from exc
            except ValueError as exc:  # UnicodeDecodeError, JSONDecodeError
                raise HephError(f"Malformed Codex stream event: {exc}") from exc


def _error(event: object, *path: str) -> HephError:
    message = _at(event, *path, "message")
    return HephError(f"Codex error: {message if isinstance(message, str) else event}"[:600])


def _end(event: object) -> Iterator[Finish | Usage]:
    match _at(event, "type"), _at(event, "response", "incomplete_details", "reason"):
        case "response.completed", _:
            yield Finish("stop")
        case _, "max_output_tokens":
            yield Finish("length")
        case _, str(reason):
            yield Finish(reason)
        case _:
            raise _error(event, "response", "error")
    prompt = _at(event, "response", "usage", "input_tokens")
    completion = _at(event, "response", "usage", "output_tokens")
    if isinstance(prompt, int) and isinstance(completion, int):
        yield Usage(prompt, completion)


def _events(response: http.client.HTTPResponse) -> Iterator[Delta | Usage | Finish]:
    for raw in iter(response.readline, b""):
        line = raw.decode().strip()
        if not line.startswith("data:"):
            continue
        event: object = json.loads(line.removeprefix("data:"))
        match _at(event, "type"), _at(event, "delta"):
            case "response.output_text.delta", str(text):
                yield Delta(text, reasoning=False)
            case "response.reasoning_summary_text.delta", str(text):
                yield Delta(text, reasoning=True)
            case "response.completed" | "response.incomplete", _:
                yield from _end(event)
                return
            case "response.failed", _:
                raise _error(event, "response", "error")
            case "error", _:
                raise _error(event)
            case _:
                pass
    raise HephError("Codex stream ended before the response completed.")
