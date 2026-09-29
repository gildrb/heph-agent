"""Interactive input: fuzzy slash commands, arrow-key pickers, and a status line."""

import sys
from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass
from getpass import getpass
from pathlib import Path

from prompt_toolkit import PromptSession, prompt
from prompt_toolkit.application import Application
from prompt_toolkit.buffer import Buffer
from prompt_toolkit.completion import CompleteEvent, Completer, Completion, PathCompleter
from prompt_toolkit.document import Document
from prompt_toolkit.filters import has_completions
from prompt_toolkit.formatted_text import StyleAndTextTuples
from prompt_toolkit.history import FileHistory, History, InMemoryHistory
from prompt_toolkit.key_binding import KeyBindings, KeyPressEvent
from prompt_toolkit.layout import HSplit, Layout, Window
from prompt_toolkit.layout.controls import BufferControl, FormattedTextControl
from prompt_toolkit.styles import Style

from heph.fuzzy import order, rank

PROMPT = "\N{SINGLE RIGHT-POINTING ANGLE QUOTATION MARK} "
# name: (arguments, what it does); the order is the menu order for an empty query
COMMANDS: dict[str, tuple[str, str]] = {
    "model": ("", "pick the model, add a local server, or log in"),
    "armory": ("", "open or create an armory"),
    "add": ("[path]", "copy a file or folder into this armory"),
    "new": ("", "start a new chat"),
    "sources": ("", "show the passages behind the last answer"),
    "login": ("", "log in to OpenAI, OpenRouter, DeepSeek, Z.AI or ChatGPT"),
    "logout": ("", "remove a login"),
    "help": ("", "show commands"),
    "exit": ("", "quit (Ctrl-D works too)"),
}
_ALIASES = {"quit": "exit"}
_STYLE = Style.from_dict(
    {
        "bottom-toolbar": "noreverse fg:ansibrightblack",
        "completion-menu": "bg:default",
        "completion-menu.completion": "bg:default fg:default",
        "completion-menu.completion.current": "reverse",
        "completion-menu.meta.completion": "bg:default fg:ansibrightblack",
        "completion-menu.meta.completion.current": "reverse",
        "scrollbar.background": "bg:default",
        "scrollbar.button": "bg:ansibrightblack",
        "pick.title": "bold",
        "pick.current": "reverse",
        "pick.detail": "fg:ansibrightblack",
    }
)


@dataclass(frozen=True, slots=True)
class Option:
    value: str
    label: str
    detail: str = ""


class _Picker:
    """A list under a filter line: typing narrows it (fuzzy), arrows move, Enter picks."""

    def __init__(self, title: str, options: Sequence[Option], rows: int) -> None:
        self.title = title
        self.options = options
        self.rows = rows
        self.selected = 0
        self.query = Buffer(multiline=False, on_text_changed=self._reset)

    def _reset(self, _buffer: Buffer) -> None:
        self.selected = 0

    def shown(self) -> tuple[list[Option], int]:
        """The matches that fit on screen, and how many more there are."""
        texts = [f"{option.label} {option.detail}" for option in self.options]
        found = [self.options[index] for index in order(self.query.text, texts)]
        return found[: self.rows], max(len(found) - self.rows, 0)

    def lines(self) -> StyleAndTextTuples:
        shown, more = self.shown()
        self.selected = min(self.selected, max(len(shown) - 1, 0))
        out: StyleAndTextTuples = []
        width = max((len(option.label) for option in shown), default=0)
        for index, option in enumerate(shown):
            current = index == self.selected
            out.append(("class:pick.current" if current else "", f" {option.label.ljust(width)} "))
            if option.detail:
                out.append(("class:pick.detail", f"  {option.detail}"))
            out.append(("", "\n"))
        if not shown:
            out.append(("class:pick.detail", " nothing matches\n"))
        keys = "type to filter   ↑↓ move   Enter pick   Esc skip"
        out.append(("class:pick.detail", f" +{more} more   {keys}" if more else f" {keys}"))
        return out

    def run(self) -> str | None:
        keys = KeyBindings()

        @keys.add("up")
        @keys.add("c-p")
        @keys.add("s-tab")
        def _up(event: KeyPressEvent) -> None:
            del event
            self.selected = max(self.selected - 1, 0)

        @keys.add("down")
        @keys.add("c-n")
        @keys.add("tab")
        def _down(event: KeyPressEvent) -> None:
            del event
            self.selected = min(self.selected + 1, max(len(self.shown()[0]) - 1, 0))

        @keys.add("enter")
        def _pick(event: KeyPressEvent) -> None:
            shown = self.shown()[0]
            event.app.exit(result=shown[self.selected].value if shown else None)

        @keys.add("escape", eager=True)
        @keys.add("c-c")
        @keys.add("c-d")
        def _skip(event: KeyPressEvent) -> None:
            event.app.exit(result=None)

        def prefix(_line: int, _wrap: int) -> StyleAndTextTuples:
            return [("", PROMPT)]

        layout = Layout(
            HSplit(
                [
                    Window(FormattedTextControl([("class:pick.title", self.title)]), height=1),
                    Window(BufferControl(self.query), height=1, get_line_prefix=prefix),
                    Window(FormattedTextControl(self.lines)),
                ]
            ),
            focused_element=self.query,
        )
        app: Application[str | None] = Application(
            layout=layout, key_bindings=keys, style=_STYLE, erase_when_done=True
        )
        return app.run()


def pick(title: str, options: Sequence[Option], rows: int = 12) -> str | None:
    """Lets the user choose one option; None when they skip (Esc, Ctrl-C) or there is none."""
    if not options:
        return None
    if sys.stdin.isatty():
        return _Picker(title, options, rows).run()
    print(title)
    for number, option in enumerate(options, 1):
        print(f"  {number}  {option.label}  {option.detail}".rstrip())
    try:
        answer = input(PROMPT).strip()
    except EOFError:
        return None
    if answer.isdigit() and 1 <= int(answer) <= len(options):
        return options[int(answer) - 1].value
    found = order(answer, [option.label for option in options]) if answer else []
    return options[found[0]].value if found else None


def command(name: str) -> str | None:
    """The command a typed name most likely means (exact, prefix, letters in order, typos)."""
    ranked = rank(name, [*COMMANDS, *_ALIASES])
    return _ALIASES.get(ranked[0], ranked[0]) if ranked else None


def help_text() -> str:
    usage = {name: f"/{name} {args}".rstrip() for name, (args, _) in COMMANDS.items()}
    width = max(map(len, usage.values())) + 2
    lines = [usage[name].ljust(width) + about for name, (_, about) in COMMANDS.items()]
    lines.append("Commands are forgiving: /ar, /arm and /armroy all find /armory.")
    return "\n".join(lines)


class _Completer(Completer):
    """Fuzzy slash commands, then fuzzy names for their argument, or paths after /add."""

    def __init__(self, choices: Mapping[str, Callable[[], list[str]]]) -> None:
        self._choices = choices
        self._paths = PathCompleter(expanduser=True)

    def get_completions(
        self, document: Document, complete_event: CompleteEvent
    ) -> Iterable[Completion]:
        text = document.text_before_cursor
        if not text.startswith("/"):
            return
        name, space, arg = text[1:].partition(" ")
        if not space:
            for match in rank(name, list(COMMANDS)):
                args, about = COMMANDS[match]
                yield Completion(
                    f"/{match}",
                    start_position=-len(text),
                    display=f"/{match} {args}".rstrip(),
                    display_meta=about,
                )
            return
        match command(name):
            case "add":
                token = arg.rsplit(" ", 1)[-1]
                yield from self._paths.get_completions(Document(token), complete_event)
            case str(found) if found in self._choices:
                for choice in rank(arg.strip(), self._choices[found]()):
                    yield Completion(choice, start_position=-len(arg))
            case _:
                return


class Input:
    """Reads one line: in a terminal with history, completion and a status line."""

    def __init__(
        self, choices: Mapping[str, Callable[[], list[str]]], status: Callable[[], str]
    ) -> None:
        self._completer = _Completer(choices)
        self._status = status
        self._session = self._new(InMemoryHistory())

    def _new(self, history: History) -> PromptSession[str]:
        keys = KeyBindings()

        @keys.add("escape", eager=True, filter=has_completions)
        def _close_menu(event: KeyPressEvent) -> None:
            event.current_buffer.cancel_completion()

        return PromptSession(
            history=history,
            completer=self._completer,
            complete_while_typing=True,
            bottom_toolbar=self._status,
            key_bindings=keys,
            style=_STYLE,
        )

    def history(self, path: Path) -> None:
        """Switches to an armory's input history file."""
        self._session = self._new(FileHistory(str(path)))

    def read(self) -> str:
        if not sys.stdin.isatty():
            return input(PROMPT)
        return self._session.prompt(PROMPT)

    def ask(self, label: str, default: str = "") -> str:
        """One plain answer, outside history and completion; Enter alone keeps the default."""
        if not sys.stdin.isatty():
            return input(label) or default
        return prompt(label, default=default, style=_STYLE)

    def path(self, label: str) -> str:
        """One file or folder path, completed as it is typed."""
        if not sys.stdin.isatty():
            return input(label)
        return prompt(label, completer=PathCompleter(expanduser=True), style=_STYLE)

    def secret(self, label: str) -> str:
        """One hidden answer (API keys)."""
        return getpass(label) if not sys.stdin.isatty() else prompt(label, is_password=True)
