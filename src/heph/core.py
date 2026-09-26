"""Client for the packaged `heph-core` binary (protocol v1, see core/main.bend)."""

import os
import subprocess
from collections.abc import Sequence
from dataclasses import dataclass
from importlib import resources
from pathlib import Path
from typing import Literal

from heph import HephError

_TIMEOUT_SECONDS = 600
type Verdict = Literal["ok", "badid", "empty", "missing"]


class HephCoreError(HephError):
    """heph-core is missing, crashed, or rejected a request."""


@dataclass(frozen=True, slots=True)
class Chunk:
    doc: int
    start: int
    end: int


@dataclass(frozen=True, slots=True)
class Check:
    quote: int
    verdict: Verdict
    offset: int


@dataclass(frozen=True, slots=True)
class Hit:
    query: int
    rank: int
    chunk: int
    score: int


@dataclass(frozen=True, slots=True)
class Posting:
    chunk: int
    tf: int
    dl: int


@dataclass(frozen=True, slots=True)
class Response:
    chunks: tuple[Chunk, ...]
    checks: tuple[Check, ...]
    hits: tuple[Hit, ...]


def binary() -> Path:
    path = Path(str(resources.files("heph").joinpath("_bin", "heph-core"))).resolve()
    if not path.is_file() or not os.access(path, os.X_OK):
        raise HephCoreError(
            f"heph-core binary missing at {path}. Reinstall heph, or build it from a checkout "
            "with core/build.sh."
        )
    return path


def doc_frame(text: bytes) -> bytes:
    return b"D %d\n" % len(text) + text


def evidence_frame(text: bytes) -> bytes:
    return b"E %d\n" % len(text) + text


def quote_frame(evidence: int, quote: bytes) -> bytes:
    return b"Q %d %d\n" % (evidence, len(quote)) + quote


def rank_frame(k: int, n: int, total: int, terms: Sequence[Sequence[Posting]]) -> bytes:
    """One rank query over unique terms; each term is its full postings list."""
    parts = [b"R %d %d %d %d\n" % (k, n, total, len(terms))]
    for postings in terms:
        parts.append(b"T %d\n" % len(postings))
        parts.extend(b"%d %d %d\n" % (p.chunk, p.tf, p.dl) for p in postings)
    return b"".join(parts)


def run(request: bytes) -> Response:
    """Runs one request through heph-core; fixed argv, no shell, stdin/stdout only."""
    try:
        done = subprocess.run(
            [str(binary()), "--gpu", "off"],
            input=request,
            capture_output=True,
            check=False,
            timeout=_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as exc:
        raise HephCoreError(f"heph-core timed out after {_TIMEOUT_SECONDS} s") from exc
    if done.returncode != 0:
        detail = done.stderr.decode(errors="replace").strip() or "no message"
        raise HephCoreError(f"heph-core failed (exit {done.returncode}): {detail}")
    return _parse(done.stdout)


def _parse(output: bytes) -> Response:
    chunks: list[Chunk] = []
    checks: list[Check] = []
    hits: list[Hit] = []
    for line in output.decode("ascii").splitlines():
        match line.split(" "):
            case ["c", doc, start, end]:
                chunks.append(Chunk(int(doc), int(start), int(end)))
            case ["v", quote, "ok", offset]:
                checks.append(Check(int(quote), "ok", int(offset)))
            case ["v", quote, ("badid" | "empty" | "missing") as verdict]:
                checks.append(Check(int(quote), verdict, 0))
            case ["h", query, rank, chunk, score]:
                hits.append(Hit(int(query), int(rank), int(chunk), int(score)))
            case _:
                raise HephCoreError(f"heph-core sent an unexpected line: {line!r}")
    return Response(tuple(chunks), tuple(checks), tuple(hits))
