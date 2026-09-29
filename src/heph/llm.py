"""Stdlib OpenAI-compatible client (SSE chat completions, GET /models) and the client protocol."""

import http.client
import json
import ssl
from collections.abc import Iterator
from dataclasses import dataclass
from http import HTTPStatus
from typing import Protocol
from urllib.parse import urlsplit

import certifi

from heph import HephError

_CONNECT_TIMEOUT = 10.0
_READ_TIMEOUT = 300.0


class AuthError(HephError):
    """The server refused the request's key (HTTP 401 or 403)."""


@dataclass(frozen=True, slots=True)
class Usage:
    prompt_tokens: int
    completion_tokens: int


@dataclass(frozen=True, slots=True)
class Delta:
    text: str
    reasoning: bool


@dataclass(frozen=True, slots=True)
class Finish:
    """Why the model stopped: "stop", "length" (hit max_tokens), or a server-specific reason."""

    reason: str


type Message = dict[str, str]


class ModelClient(Protocol):
    """What answering needs from a login: its models and one streamed chat completion."""

    def models(self) -> list[str]: ...

    def stream(
        self, model: str, messages: list[Message], max_tokens: int, temperature: float
    ) -> Iterator[Delta | Usage | Finish]: ...


def _at(value: object, *path: str | int) -> object:
    for key in path:
        match value, key:
            case {**fields}, str():
                value = fields.get(key)
            case [*items], int(index) if len(items) > index:
                value = items[index]
            case _:
                return None
    return value


@dataclass(frozen=True, slots=True)
class Client:
    base_url: str
    api_key: str

    def _open(self, method: str, path: str, body: bytes | None) -> http.client.HTTPResponse:
        url = f"{self.base_url}{path}"
        parts = urlsplit(url)
        host = parts.hostname or ""
        if parts.scheme == "https":
            context = ssl.create_default_context(cafile=certifi.where())
            conn: http.client.HTTPConnection = http.client.HTTPSConnection(
                host, parts.port, timeout=_CONNECT_TIMEOUT, context=context
            )
        else:
            conn = http.client.HTTPConnection(host, parts.port, timeout=_CONNECT_TIMEOUT)
        headers = {"Content-Type": "application/json", "Accept": "application/json"}
        if self.api_key:
            headers["Authorization"] = f"Bearer {self.api_key}"
        target = parts.path + (f"?{parts.query}" if parts.query else "")
        try:
            conn.request(method, target, body=body, headers=headers)
            if conn.sock is not None:
                conn.sock.settimeout(_READ_TIMEOUT)
            response = conn.getresponse()
        except (ssl.SSLError, TimeoutError, http.client.HTTPException) as exc:
            raise HephError(f"{method} {url} failed: {exc}") from exc
        except OSError as exc:
            raise HephError(
                f"No model server at {self.base_url} ({exc.strerror or exc}). Start it, or "
                "pick another model with /model."
            ) from exc
        if response.status >= HTTPStatus.BAD_REQUEST:
            snippet = " ".join(response.read(800).decode(errors="replace").split())
            message = f"{method} {url} returned HTTP {response.status}: {snippet}"
            if response.status in {401, 403}:
                raise AuthError(f"{message} The key is missing or wrong: /login saves one.")
            raise HephError(message)
        return response

    def models(self) -> list[str]:
        with self._open("GET", "/models", None) as response:
            try:
                data: object = json.loads(response.read())
            except (http.client.HTTPException, OSError, ValueError) as exc:
                raise HephError(f"Bad /models response from {self.base_url}: {exc}") from exc
        entries = _at(data, "data")
        ids = [_at(e, "id") for e in entries] if isinstance(entries, list) else []
        return [i for i in ids if isinstance(i, str)]

    def stream(
        self, model: str, messages: list[Message], max_tokens: int, temperature: float
    ) -> Iterator[Delta | Usage | Finish]:
        """Yields reasoning/content deltas, the finish reason, and usage when reported."""
        body = {
            "model": model,
            "messages": messages,
            "stream": True,
            "stream_options": {"include_usage": True},
            "max_tokens": max_tokens,
            "temperature": temperature,
        }
        with self._open("POST", "/chat/completions", json.dumps(body).encode()) as response:
            try:
                yield from _events(response)
            except (http.client.HTTPException, OSError) as exc:
                raise HephError(f"Model stream from {self.base_url} broke off: {exc}") from exc
            except ValueError as exc:  # UnicodeDecodeError, JSONDecodeError
                raise HephError(f"Malformed stream event from {self.base_url}: {exc}") from exc


def _events(response: http.client.HTTPResponse) -> Iterator[Delta | Usage | Finish]:
    for raw in iter(response.readline, b""):
        line = raw.decode().strip()
        if not line.startswith("data:"):
            continue
        payload = line.removeprefix("data:").strip()
        if payload == "[DONE]":
            return
        event: object = json.loads(payload)
        error = _at(event, "error")
        if error is not None:
            message = _at(error, "message")
            raise HephError(f"Model server error: {message or error}")
        delta = _at(event, "choices", 0, "delta")
        for key in ("reasoning_content", "reasoning"):
            text = _at(delta, key)
            if isinstance(text, str) and text:
                yield Delta(text, reasoning=True)
                break
        text = _at(delta, "content")
        if isinstance(text, str) and text:
            yield Delta(text, reasoning=False)
        reason = _at(event, "choices", 0, "finish_reason")
        if isinstance(reason, str) and reason:
            yield Finish(reason)
        prompt = _at(event, "usage", "prompt_tokens")
        completion = _at(event, "usage", "completion_tokens")
        if isinstance(prompt, int) and isinstance(completion, int):
            yield Usage(prompt, completion)
