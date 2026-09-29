"""Append-only terminal output: spinner status, Markdown blocks, citations, sources, stats."""

import re
from collections.abc import Iterator, Sequence
from contextlib import contextmanager

from rich.console import Console
from rich.markdown import Markdown
from rich.segment import Segment, Segments
from rich.status import Status as Spinner
from rich.style import Style
from rich.text import Text

from heph.answer import Citation, Evidence, Result, Status, location
from heph.index import Report

SEP = "   "  # separates fields on one line: spacing, no dots

_STYLE: dict[Status, str] = {
    "verified": "green",
    "unquoted": "yellow",
    "failed": "red",
    "badid": "red",
}
_MARK: dict[Status, str] = {"verified": "✓", "unquoted": "?", "failed": "✗", "badid": "✗"}
_NOTE: dict[Status, str] = {
    "unquoted": "no quote",
    "failed": "quote not found",
    "badid": "no such evidence",
}
_SEVERITY: tuple[Status, ...] = ("verified", "unquoted", "failed", "badid")


def console(*, stderr: bool = False) -> Console:
    """Rich console; honors NO_COLOR and degrades to plain text when not a terminal."""
    return Console(stderr=stderr, highlight=False, soft_wrap=True)


def _placeholder(number: int, width: int) -> str:
    """ASCII token Markdown leaves alone, same cell width as the label it stands for."""
    letters = ""
    while True:
        number, digit = divmod(number, 26)
        letters += chr(ord("A") + digit)
        if not number:
            break
    return f"Zq{letters}".ljust(width, "q")


class Renderer:
    def __init__(self, out: Console) -> None:
        self.console = out
        self._spinner: Spinner | None = None
        self._thought = ""

    @contextmanager
    def status(self, text: str) -> Iterator[None]:
        """Transient one-line spinner (terminals only); printed blocks scroll above it."""
        if not self.console.is_terminal:
            yield
            return
        self._thought = ""
        self._spinner = self.console.status(Text(text, style="dim"))
        try:
            with self._spinner:
                yield
        finally:
            self._spinner = None

    def fullscreen(self) -> None:
        """Starts a session on a blank terminal: clears the screen and its scrollback."""
        if self.console.is_terminal:
            self.console.file.write("\x1b[H\x1b[2J\x1b[3J")
            self.console.file.flush()

    def update(self, text: str) -> None:
        if self._spinner is not None:
            self._spinner.update(Text(text, style="dim"))

    def reasoning(self, text: str) -> None:
        self._thought = (self._thought + text)[-400:]
        tail = " ".join(self._thought.split())[-max(20, self.console.width - 16) :]
        self.update(f"Thinking{SEP}{tail}")

    def block(self, text: str, offset: int, citations: Sequence[Citation]) -> None:
        self.update("Writing")
        if not self.console.is_terminal:
            self.console.file.write(text)
            self.console.file.flush()
            return
        spans: dict[tuple[int, int], list[Citation]] = {}
        for citation in citations:
            spans.setdefault((citation.start - offset, citation.end - offset), []).append(citation)
        labels: dict[str, tuple[str, Style]] = {}
        source: list[str] = []
        position = 0
        for (start, end), group in spans.items():
            label = "[" + ", ".join(f"E{c.eid}" for c in group) + "]"
            worst = max((c.status for c in group), key=_SEVERITY.index)
            token = _placeholder(len(labels), len(label))
            labels[token] = (label, Style.parse(_STYLE[worst]))
            source += [text[position:start], token]
            position = end
        source.append(text[position:])
        lines = self.console.render_lines(Markdown("".join(source).strip()), pad=False)
        tokens = "|".join(sorted(labels, key=lambda token: (len(token), token), reverse=True))
        pattern = re.compile(f"({tokens})") if labels else None
        segments: list[Segment] = []
        for line in lines:
            for segment in line:
                if pattern is None or segment.control:
                    segments.append(segment)
                    continue
                for part in pattern.split(segment.text):
                    if part in labels:
                        label, style = labels[part]
                        segments.append(Segment(label, (segment.style or Style()) + style))
                    elif part:
                        segments.append(Segment(part, segment.style))
            segments.append(Segment.line())
        self.console.print(Segments(segments), end="")

    def finish(self, result: Result) -> None:
        """Sources footer (one line per distinct citation) and the stats line."""
        seen: dict[tuple[int, str | None], Citation] = {}
        for citation in result.citations:
            seen.setdefault((citation.eid, citation.quote), citation)
        self.console.print()
        for c in seen.values():
            line = Text.assemble((f"E{c.eid} ", "bold"))
            if c.source is not None:
                line.append(location(c.source, c.page) + " ", style="dim")
            line.append(_MARK[c.status], style=_STYLE[c.status])
            if c.quote:
                # verification ignores whitespace runs, so show the quote on one line
                line.append(f' "{" ".join(c.quote.split())}"', style="italic")
            if c.status in _NOTE:
                line.append(f" ({_NOTE[c.status]})", style=_STYLE[c.status])
            self.console.print(line)
        if result.truncated:
            self.console.print(Text("Answer cut off at max_tokens.", style="yellow"))
        stats = [result.model]
        if result.usage is not None:
            tokens = result.usage.completion_tokens
            stats.append(f"{tokens} tok")
            # end to end, including prefill: some servers buffer the whole stream
            stats.append(f"{tokens / result.seconds:.1f} tok/s")
        stats.append(f"{result.seconds:.1f} s")
        self.console.print(Text(SEP.join(stats), style="dim"))

    def sources(self, evidence: Sequence[Evidence]) -> None:
        if not evidence:
            self.console.print(Text("No evidence yet.", style="dim"))
        for item in evidence:
            line = Text.assemble((f"E{item.eid} ", "bold"))
            line.append(location(item.source, item.page))
            line.append("  " + " ".join(item.text.split())[:100], style="dim")
            self.console.print(line, overflow="ellipsis", no_wrap=True)

    def report(self, report: Report, *, quiet: bool) -> None:
        """Index outcome; when quiet, only if something changed or was skipped."""
        if quiet and not (report.built or report.removed or report.skipped):
            return
        files = f"{report.files} file{'' if report.files == 1 else 's'}"
        summary = SEP.join((files, f"{report.built} indexed", f"{report.removed} removed"))
        if report.skipped:
            summary += f"{SEP}{len(report.skipped)} skipped"
        self.console.print(Text(summary, style="dim"))
        for source, reason in report.skipped:
            self.console.print(Text(f"  skipped {source}: {reason}", style="yellow"))

    def note(self, text: str, style: str = "dim") -> None:
        self.console.print(Text(text, style=style))
