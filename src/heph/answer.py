"""One turn: retrieve -> prompt -> stream -> verify citations (heph-core) -> result."""

import re
import time
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Literal, Protocol

from heph import HephError, core
from heph.config import Config
from heph.index import Index
from heph.llm import Client, Finish, Message, Usage

type Status = Literal["verified", "failed", "unquoted", "badid"]
_STATUS: dict[core.Verdict, Status] = {
    "ok": "verified",
    "missing": "failed",
    "empty": "unquoted",
    "badid": "badid",
}
_MAX_HITS = 12
_FENCE = re.compile(r"(`{3,}|~{3,})(.*)")
_HISTORY_TURNS = 3
_CITATION = re.compile(
    r"[\[【]\s*[Ee](\d+)\s*(?::\s*[\"“”„«]([^\]】]*?)[\"“”»]\s*)?[\]】]"
    r"|[\[【]\s*([Ee]\d+(?:\s*[,;]\s*[Ee]\d+)+)\s*[\]】]"
)
SYSTEM = """You are Heph. Answer the question using only the evidence items in the user's \
message. Each item starts with a label such as [E1] followed by its source.

Rules:
- Support every factual claim with a citation of the form [E3: "words copied from E3"]. The \
quoted words must be copied exactly from that evidence item: one short contiguous passage, no \
ellipses, no paraphrase, no added formatting.
- Cite only evidence labels that exist. Never invent sources or quotes.
- If the evidence does not answer the question, say so plainly instead of guessing.
- Answer in the language of the question, concisely, using Markdown."""


@dataclass(frozen=True, slots=True)
class Evidence:
    eid: int
    source: str
    page: int | None
    text: str


@dataclass(frozen=True, slots=True)
class Citation:
    eid: int
    quote: str | None
    start: int
    end: int
    status: Status
    source: str | None
    page: int | None


@dataclass(frozen=True, slots=True)
class Result:
    question: str
    answer: str
    model: str
    evidence: tuple[Evidence, ...]
    citations: tuple[Citation, ...]
    usage: Usage | None
    seconds: float
    truncated: bool

    def json(self) -> dict[str, object]:
        return {
            "answer": self.answer,
            "citations": [
                {
                    "id": f"E{c.eid}",
                    "source": c.source,
                    "page": c.page,
                    "quote": c.quote,
                    "status": c.status,
                }
                for c in self.citations
            ],
            "evidence": [
                {"id": f"E{e.eid}", "source": e.source, "page": e.page, "text": e.text}
                for e in self.evidence
            ],
            "truncated": self.truncated,
            "usage": None
            if self.usage is None
            else {
                "prompt_tokens": self.usage.prompt_tokens,
                "completion_tokens": self.usage.completion_tokens,
            },
        }


class Events(Protocol):
    def reasoning(self, text: str) -> None: ...

    def block(self, text: str, offset: int, citations: Sequence[Citation]) -> None: ...


def location(source: str, page: int | None) -> str:
    return source if page is None else f"{source} p.{page}"


def retrieve(index: Index, question: str, budget_tokens: int) -> tuple[Evidence, ...]:
    """Top chunks (at most 12) until the evidence token budget (bytes/4) is filled."""
    evidence: list[Evidence] = []
    used = 0
    for chunk in index.search(question, _MAX_HITS):
        cost = len(chunk.text.encode()) // 4
        if evidence and used + cost > budget_tokens:
            break
        evidence.append(Evidence(len(evidence) + 1, chunk.source, chunk.page, chunk.text))
        used += cost
    return tuple(evidence)


def messages(
    history: Sequence[tuple[str, str]], evidence: Sequence[Evidence], question: str
) -> list[Message]:
    """Prefix-stable prompt: constant system, prior turns without evidence, then this turn."""
    out: list[Message] = [{"role": "system", "content": SYSTEM}]
    for asked, answered in history[-_HISTORY_TURNS:]:
        out += [{"role": "user", "content": asked}, {"role": "assistant", "content": answered}]
    items = [f"[E{e.eid}] {location(e.source, e.page)}\n{e.text.strip()}" for e in evidence]
    body = "\n\n".join(items) if items else "(no matching evidence in the armory)"
    out.append({"role": "user", "content": f"Evidence:\n\n{body}\n\nQuestion: {question}"})
    return out


def cite(evidence: Sequence[Evidence], text: str, base: int) -> list[Citation]:
    """Parses citations in `text` and verifies quotes with heph-core; offsets shifted by base."""
    found: list[tuple[int, str | None, int, int]] = []
    for match in _CITATION.finditer(text):
        start, end = match.start() + base, match.end() + base
        if match.group(3):
            ids = re.finditer(r"\d+", match.group(3))
            found += [(int(i.group()), None, start, end) for i in ids]
        else:
            found.append((int(match.group(1)), match.group(2), start, end))
    quoted = [(i, eid, quote) for i, (eid, quote, _, _) in enumerate(found) if quote]
    verdicts: dict[int, core.Verdict] = {}
    if quoted:
        frames = [core.evidence_frame(e.text.encode()) for e in evidence]
        # Any out-of-range id the model writes maps to the first id the core must reject.
        bad = len(evidence)
        frames += [
            core.quote_frame(eid - 1 if 0 < eid <= bad else bad, quote.encode())
            for _, eid, quote in quoted
        ]
        checks = core.run(b"".join(frames)).checks
        verdicts = {quoted[c.quote][0]: c.verdict for c in checks}
    citations: list[Citation] = []
    for i, (eid, quote, start, end) in enumerate(found):
        item = evidence[eid - 1] if 0 < eid <= len(evidence) else None
        status: Status = _STATUS[verdicts[i]] if i in verdicts else "unquoted"
        if item is None:
            status = "badid"
        source, page = (item.source, item.page) if item else (None, None)
        citations.append(Citation(eid, quote, start, end, status, source, page))
    return citations


@dataclass(slots=True)
class _Blocks:
    """Splits streamed Markdown into complete blocks: blank line outside a code fence."""

    text: str = ""
    start: int = 0
    line: int = 0
    fence: str = ""

    def feed(self, delta: str) -> list[tuple[int, int]]:
        self.text += delta
        done: list[tuple[int, int]] = []
        while (newline := self.text.find("\n", self.line)) >= 0:
            content = self.text[self.line : newline].strip()
            self.line = newline + 1
            marker = _FENCE.match(content)
            if self.fence:
                if marker and marker[1].startswith(self.fence) and not marker[2].strip():
                    self.fence = ""
            elif marker:
                self.fence = marker[1]
            elif not content:
                if self.text[self.start : self.line].strip():
                    done.append((self.start, self.line))
                self.start = self.line
        return done

    def flush(self) -> list[tuple[int, int]]:
        return [(self.start, len(self.text))] if self.text[self.start :].strip() else []


@dataclass(frozen=True, slots=True)
class Engine:
    client: Client
    model: str
    config: Config
    index: Index


def ask(
    engine: Engine, history: Sequence[tuple[str, str]], question: str, events: Events
) -> Result:
    """Runs one model call and verifies citations block by block as they complete."""
    started = time.monotonic()
    config = engine.config
    evidence = retrieve(engine.index, question, config.evidence_tokens)
    prompt = messages(history, evidence, question)
    blocks = _Blocks()
    citations: list[Citation] = []
    usage: Usage | None = None
    finish: str | None = None

    def emit(spans: list[tuple[int, int]]) -> None:
        for start, end in spans:
            found = cite(evidence, blocks.text[start:end], start)
            citations.extend(found)
            events.block(blocks.text[start:end], start, found)

    stream = engine.client.stream(engine.model, prompt, config.max_tokens, config.temperature)
    for item in stream:
        if isinstance(item, Usage):
            usage = item
            continue
        if isinstance(item, Finish):
            finish = item.reason
            continue
        if item.reasoning:
            events.reasoning(item.text)
        else:
            emit(blocks.feed(item.text))
    emit(blocks.flush())
    if not blocks.text.strip():
        if finish == "length":
            raise HephError(
                f"The model used all {config.max_tokens} max_tokens before answering "
                f"(reasoning models think first). Raise max_tokens in {config.path}."
            )
        raise HephError(f"The model returned an empty answer (finish reason: {finish}).")
    return Result(
        question=question,
        answer=blocks.text,
        model=engine.model,
        evidence=evidence,
        citations=tuple(citations),
        usage=usage,
        seconds=time.monotonic() - started,
        truncated=finish == "length",
    )
