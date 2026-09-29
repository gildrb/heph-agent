"""Interactive input: fuzzy slash commands and a status line pinned to the bottom."""

import sys
from collections.abc import Callable, Iterable, Mapping
from getpass import getpass
from pathlib import Path

from prompt_toolkit import PromptSession, prompt
from prompt_toolkit.completion import CompleteEvent, Completer, Completion, PathCompleter
from prompt_toolkit.document import Document
from prompt_toolkit.filters import has_completions
from prompt_toolkit.history import FileHistory, History, InMemoryHistory
from prompt_toolkit.key_binding import KeyBindings, KeyPressEvent
from prompt_toolkit.styles import Style

from heph.fuzzy import rank

PROMPT = "\N{SINGLE RIGHT-POINTING ANGLE QUOTATION MARK} "
# name: (arguments, what it does); the order is the menu order for an empty query
COMMANDS: dict[str, tuple[str, str]] = {
    "model": ("[model|url]", "list your logins' models, switch, or add a local server by URL"),
    "armory": ("[name]", "list armories, or open one by number, name or path"),
    "init": ("[name]", "make this folder an armory, or create one in the armory home"),
    "add": ("<path>...", "copy files or folders into the armory, then index"),
    "login": ("[provider]", "log in: local server URL, OpenAI, OpenRouter, DeepSeek, Z.AI, Codex"),
    "logout": ("<login>", "remove a login and the key Heph saved for it"),
    "new": ("", "start a new chat (forget the conversation so far)"),
    "sources": ("", "show the evidence given to the model for the last answer"),
    "index": ("", "re-index the armory"),
    "help": ("", "show this help"),
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
    }
)


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

    def ask(self, label: str) -> str:
        """One plain answer, outside history and completion."""
        return input(label) if not sys.stdin.isatty() else prompt(label, style=_STYLE)

    def secret(self, label: str) -> str:
        """One hidden answer (API keys)."""
        return getpass(label) if not sys.stdin.isatty() else prompt(label, is_password=True)
