"""Text extraction: plain text, PDF (per page), DOCX/PPTX/XLSX/ODT/ODS."""

import io
import re
import unicodedata
import zipfile
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from pathlib import PurePosixPath
from xml.etree import ElementTree as ET

import pypdfium2
from defusedxml.ElementTree import DefusedXMLParser

from heph import HephError

MAX_SOURCE_BYTES = 50 * 2**20
MAX_TEXT_BYTES = 5 * 2**20
_MAX_MEMBERS = 2000
_MAX_MEMBER_BYTES = 20 * 2**20
_MAX_EXPANDED_BYTES = 100 * 2**20
_UNSUPPORTED = frozenset({".doc", ".ppt", ".xls", ".odp", ".rtf"})
_SPACING = {"tab": "\t", "s": " ", "line-break": "\n"}
_WORD = re.compile(r"\w+")
_BREAK = re.compile(r"(\w+)\ufffe(\w+)")


@dataclass(frozen=True, slots=True)
class Extracted:
    text: str
    pages: tuple[int, ...]
    """UTF-8 byte offset where each page starts; empty for unpaginated formats."""


def _clean(text: str) -> str:
    text = text.replace("\r\n", "\n").replace("\r", "\n").replace("\x00", "")
    return unicodedata.normalize("NFC", text)


def _paged(pages: list[str]) -> Extracted:
    offsets: list[int] = []
    parts: list[str] = []
    size = 0
    for page in pages:
        offsets.append(size)
        text = _clean(page).strip("\n") + "\n\n"
        parts.append(text)
        size += len(text.encode())
    return Extracted("".join(parts), tuple(offsets))


def _pdf(data: bytes) -> Extracted:
    try:
        document = pypdfium2.PdfDocument(data)
    except pypdfium2.PdfiumError as exc:
        raise HephError(f"unreadable PDF: {exc}") from exc
    pages: list[str] = []
    try:
        for page in document:
            textpage = page.get_textpage()
            text = textpage.get_text_range()
            textpage.close()
            page.close()
            if not isinstance(text, str):
                raise HephError("PDFium returned no text")
            pages.append(text)
    finally:
        document.close()
    return _paged(_dehyphenate(pages))


def _dehyphenate(pages: list[str]) -> list[str]:
    """PDFium marks a hyphen at a line break as U+FFFE. Join the halves when the joined word
    occurs elsewhere in the document (a hyphenation), else keep a real hyphen (a compound)."""
    words = set(_WORD.findall(" ".join(pages).replace("\ufffe", " ").casefold()))

    def join(match: re.Match[str]) -> str:
        joined = match.expand(r"\1\2")
        return joined if joined.casefold() in words else match.expand(r"\1-\2")

    return [_BREAK.sub(join, page).replace("\ufffe", "-") for page in pages]


def _members(data: bytes) -> dict[str, bytes]:
    """Reads a zip container with member-count, size and path-traversal guards."""
    try:
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            infos = archive.infolist()
            if len(infos) > _MAX_MEMBERS:
                raise HephError("archive contains too many members")
            total = 0
            for info in infos:
                name = PurePosixPath(info.filename)
                if name.is_absolute() or ".." in name.parts:
                    raise HephError("archive contains a traversal path")
                if info.file_size > _MAX_MEMBER_BYTES:
                    raise HephError("archive member exceeds size limit")
                total += info.file_size
                if total > _MAX_EXPANDED_BYTES:
                    raise HephError("archive exceeds expanded size limit")
            return {
                info.filename: archive.read(info)
                for info in infos
                if info.filename.endswith(".xml")
            }
    except zipfile.BadZipFile as exc:
        raise HephError(f"invalid document archive: {exc}") from exc


def _xml(members: dict[str, bytes], name: str) -> ET.Element:
    raw = members.get(name)
    if raw is None:
        raise HephError(f"document is missing {name}")
    try:
        return ET.XML(raw, DefusedXMLParser())
    except (ET.ParseError, ValueError) as exc:
        raise HephError(f"invalid XML in {name}: {exc}") from exc


def _tag(element: ET.Element) -> str:
    return element.tag.rsplit("}", 1)[-1]


def _inline(element: ET.Element) -> str:
    """ODF paragraph text: text:tab/text:s/text:line-break become whitespace."""
    parts = [element.text or ""]
    for child in element:
        parts += [_SPACING.get(_tag(child)) or _inline(child), child.tail or ""]
    return "".join(parts)


def _para(element: ET.Element) -> str:
    runs = [e for e in element.iter() if _tag(e) in {"t", "tab"}]
    if not any(_tag(e) == "t" for e in runs):  # ODF keeps text inline
        return _inline(element)
    return "".join("\t" if _tag(e) == "tab" else e.text or "" for e in runs)


def _lines(element: ET.Element) -> Iterator[str]:
    """Paragraph lines of OOXML/ODF XML; table rows become tab-separated cells."""
    name = _tag(element)
    if name in {"tr", "table-row"}:
        cells = [" ".join(_para(c).split()) for c in element if _tag(c) in {"tc", "table-cell"}]
        if any(cells):
            yield "\t".join(cells)
    elif name in {"p", "h"}:
        text = _para(element)
        if text.strip():
            yield text
    else:
        for child in element:
            yield from _lines(child)


def _numbered(members: dict[str, bytes], prefix: str) -> list[str]:
    names = [n for n in members if n.startswith(prefix) and n[len(prefix) : -4].isdigit()]
    return sorted(names, key=lambda n: int(n[len(prefix) : -4]))


def _flow(members: dict[str, bytes], name: str) -> Extracted:
    return Extracted(_clean("\n".join(_lines(_xml(members, name)))), ())


def _pptx(members: dict[str, bytes]) -> Extracted:
    slides = _numbered(members, "ppt/slides/slide")
    return _paged(["\n".join(_lines(_xml(members, n))) for n in slides])


def _xlsx(members: dict[str, bytes]) -> Extracted:
    shared: list[str] = []
    if "xl/sharedStrings.xml" in members:
        root = _xml(members, "xl/sharedStrings.xml")
        shared = ["".join(si.itertext()) for si in root.iter() if _tag(si) == "si"]
    sheets: list[str] = []
    for name in _numbered(members, "xl/worksheets/sheet"):
        rows: list[str] = []
        for row in (e for e in _xml(members, name).iter() if _tag(e) == "row"):
            values: list[str] = []
            for cell in (c for c in row if _tag(c) == "c"):
                value = next((v.text or "" for v in cell if _tag(v) == "v"), "")
                kind = cell.get("t", "")
                if kind == "inlineStr":
                    value = "".join(cell.itertext())
                elif kind == "s" and value:
                    if not value.isdigit() or int(value) >= len(shared):
                        raise HephError(f"bad shared string index in {name}")
                    value = shared[int(value)]
                values.append(value)
            if any(values):
                rows.append("\t".join(values))
        sheets.append("\n".join(rows))
    return Extracted(_clean("\n\n".join(sheets)), ())


_OFFICE: dict[str, Callable[[dict[str, bytes]], Extracted]] = {
    ".docx": lambda members: _flow(members, "word/document.xml"),
    ".pptx": _pptx,
    ".xlsx": _xlsx,
    ".odt": lambda members: _flow(members, "content.xml"),
    ".ods": lambda members: _flow(members, "content.xml"),
}


def extract(name: str, data: bytes) -> Extracted:
    """Extracts text from a material; raises HephError with the reason it can't."""
    suffix = PurePosixPath(name).suffix.lower()
    if suffix == ".pdf":
        return _pdf(data)
    if suffix in _OFFICE:
        return _OFFICE[suffix](_members(data))
    if suffix in _UNSUPPORTED:
        raise HephError(f"{suffix} is not supported; save it as PDF, DOCX, PPTX or XLSX")
    if len(data) > MAX_TEXT_BYTES:
        raise HephError(f"text file exceeds {MAX_TEXT_BYTES // 2**20} MB")
    if b"\x00" in data[:8192]:
        raise HephError("binary file")
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError as exc:
        raise HephError(f"not UTF-8 text ({exc.reason} at byte {exc.start})") from exc
    return Extracted(_clean(text), ())
