"""Per-document index caches, tokenizer, postings and retrieval via heph-core."""

import hashlib
import json
import os
import re
import unicodedata
from bisect import bisect_right
from collections import Counter
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from heph import HephError, armory, core
from heph.extract import MAX_SOURCE_BYTES, Extracted, extract

_VERSION = 1
_BATCH_BYTES = 4 * 2**20
_TOKEN = re.compile(r"\w+")
_STOP = frozenset(
    {
        "a",
        "an",
        "and",
        "are",
        "as",
        "at",
        "be",
        "by",
        "can",
        "did",
        "do",
        "does",
        "for",
        "from",
        "has",
        "have",
        "how",
        "in",
        "is",
        "it",
        "its",
        "of",
        "on",
        "or",
        "that",
        "the",
        "this",
        "to",
        "was",
        "were",
        "what",
        "when",
        "where",
        "which",
        "who",
        "why",
        "will",
        "with",
    }
)


def tokenize(text: str) -> list[str]:
    """NFKC + casefold Unicode word runs, minus 1-char non-digits and stop words."""
    words = _TOKEN.findall(unicodedata.normalize("NFKC", text).casefold())
    return [w for w in words if (len(w) > 1 or w.isdigit()) and w not in _STOP]


@dataclass(frozen=True, slots=True)
class Chunk:
    source: str
    page: int | None
    text: str


@dataclass(frozen=True, slots=True)
class Report:
    files: int
    built: int
    removed: int
    skipped: tuple[tuple[str, str], ...]


@dataclass(frozen=True, slots=True)
class _Doc:
    source: str
    text: str
    pages: tuple[int, ...]
    chunks: tuple[tuple[int, int], ...]
    terms: tuple[dict[str, int], ...]
    lens: tuple[int, ...]

    def json(self) -> str:
        return json.dumps(
            {
                "version": _VERSION,
                "source": self.source,
                "text": self.text,
                "pages": self.pages,
                "chunks": self.chunks,
                "terms": self.terms,
                "lens": self.lens,
            },
            ensure_ascii=False,
        )


@dataclass(frozen=True, slots=True)
class Index:
    chunks: tuple[Chunk, ...]
    postings: dict[str, list[core.Posting]]
    total: int

    def search(self, query: str, k: int) -> list[Chunk]:
        """Top-k chunks by BM25 (heph-core) over the unique query terms."""
        terms = [self.postings[t] for t in dict.fromkeys(tokenize(query)) if t in self.postings]
        if not terms:
            return []
        request = core.rank_frame(k, len(self.chunks), self.total, terms)
        hits = sorted(core.run(request).hits, key=lambda hit: hit.rank)
        return [self.chunks[hit.chunk] for hit in hits]


def _ints(values: Sequence[object]) -> tuple[int, ...] | None:
    ints = tuple(v for v in values if isinstance(v, int))
    return ints if len(ints) == len(values) else None


def _span(value: object) -> tuple[int, int] | None:
    match value:
        case [int(start), int(end)]:
            return start, end
    return None


def _bag(value: object) -> dict[str, int] | None:
    match value:
        case {**fields}:
            bag = {k: v for k, v in fields.items() if isinstance(k, str) and isinstance(v, int)}
            return bag if len(bag) == len(fields) else None
    return None


def _decode(raw: object, source: str) -> _Doc | None:
    match raw:
        case {
            "version": int(version),
            "text": str(text),
            "pages": [*pages_raw],
            "chunks": [*spans],
            "terms": [*terms],
            "lens": [*lens_raw],
        }:
            pages, lens = _ints(pages_raw), _ints(lens_raw)
        case _:
            return None
    pairs = tuple(pair for span in spans if (pair := _span(span)) is not None)
    bags = tuple(bag for item in terms if (bag := _bag(item)) is not None)
    if version != _VERSION or pages is None or lens is None:
        return None
    if not len(pairs) == len(spans) == len(bags) == len(lens):
        return None
    size = len(text.encode())
    if not all(0 <= start < end <= size for start, end in pairs):
        return None
    return _Doc(source, text, pages, pairs, bags, lens)


def _load(path: Path, source: str) -> _Doc | None:
    """A missing, unreadable, truncated or outdated cache is a miss (None): rebuild it."""
    try:
        with os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW), "rb") as file:
            return _decode(json.load(file), source)
    except OSError, ValueError:
        return None


def _save(path: Path, doc: _Doc) -> None:
    tmp = path.with_suffix(".tmp")
    tmp.unlink(missing_ok=True)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW
    with os.fdopen(os.open(tmp, flags, 0o600), "w", encoding="utf-8") as file:
        file.write(doc.json())
    tmp.replace(path)


def _chunk(cache_dir: Path, batch: dict[str, tuple[str, Extracted]]) -> dict[str, _Doc]:
    """Chunks a batch of documents in one heph-core call, tokenizes and caches them."""
    texts = [extracted.text.encode() for _, extracted in batch.values()]
    spans: list[list[tuple[int, int]]] = [[] for _ in batch]
    for chunk in core.run(b"".join(core.doc_frame(text) for text in texts)).chunks:
        spans[chunk.doc].append((chunk.start, chunk.end))
    docs: dict[str, _Doc] = {}
    for (key, (source, extracted)), raw, doc_spans in zip(
        batch.items(), texts, spans, strict=True
    ):
        tokens = [tokenize(raw[s:e].decode()) for s, e in doc_spans]
        bags = tuple(dict(Counter(t)) for t in tokens)
        lens = tuple(len(t) for t in tokens)
        doc = _Doc(source, extracted.text, extracted.pages, tuple(doc_spans), bags, lens)
        _save(cache_dir / f"{key}.json", doc)
        docs[key] = doc
    return docs


def build(root: Path, progress: Callable[[int, int, str], None]) -> tuple[Index, Report]:
    """Brings the per-document caches up to date and loads the index."""
    cache_dir = root / armory.INDEX
    sources = armory.materials(root)
    docs: dict[str, _Doc] = {}
    batches: list[dict[str, tuple[str, Extracted]]] = [{}]
    batch_size = built = 0
    skipped: list[tuple[str, str]] = []
    for number, source in enumerate(sources, 1):
        progress(number, len(sources), source)
        try:
            data = armory.read_material(root, source, MAX_SOURCE_BYTES)
        except (OSError, HephError) as exc:
            skipped.append((source, str(exc)))
            continue
        key = hashlib.sha256(data).hexdigest()[:32]
        if key in docs or any(key in batch for batch in batches):
            continue
        if (cached := _load(cache_dir / f"{key}.json", source)) is not None:
            docs[key] = cached
            continue
        try:
            extracted = extract(source, data)
        except HephError as exc:
            skipped.append((source, str(exc)))
            continue
        if not extracted.text.strip():
            skipped.append((source, "no extractable text (scanned PDF or empty file)"))
            continue
        if batch_size > _BATCH_BYTES:
            batches.append({})
            batch_size = 0
        batches[-1][key] = (source, extracted)
        batch_size += len(extracted.text)
        built += 1
    for batch in batches:
        if batch:
            docs.update(_chunk(cache_dir, batch))
    removed = 0
    for path in cache_dir.iterdir():
        if path.stem not in docs:
            path.unlink()
            removed += 1
    report = Report(len(sources), built, removed, tuple(skipped))
    return _assemble(sorted(docs.values(), key=lambda d: d.source)), report


def _assemble(docs: list[_Doc]) -> Index:
    chunks: list[Chunk] = []
    postings: dict[str, list[core.Posting]] = {}
    total = 0
    for doc in docs:
        raw = doc.text.encode()
        for (start, end), bag, length in zip(doc.chunks, doc.terms, doc.lens, strict=True):
            page = bisect_right(doc.pages, start) if doc.pages else None
            for term, tf in bag.items():
                postings.setdefault(term, []).append(core.Posting(len(chunks), tf, length))
            chunks.append(Chunk(doc.source, page, raw[start:end].decode()))
            total += length
    return Index(tuple(chunks), postings, total)
