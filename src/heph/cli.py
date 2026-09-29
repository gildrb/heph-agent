"""`heph` command line: init, index, ask, config, and the interactive REPL."""

import argparse
import json
import os
import shlex
import sys
from collections.abc import Callable, Sequence
from dataclasses import dataclass, replace
from importlib.metadata import version
from pathlib import Path

from rich.text import Text

from heph import HephError, armory, codex, logins, prompt
from heph.answer import Citation, Engine, Result, ask
from heph.config import Config, load
from heph.fuzzy import rank
from heph.index import Index, build
from heph.llm import AuthError
from heph.render import SEP, Renderer, console
from heph.session import Chat

_COMMANDS = frozenset({"init", "index", "ask", "config", "repl"})


class Args(argparse.Namespace):
    command: str | None
    version: bool
    target: str | None
    armory: str | None
    question: list[str]
    json: bool


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="heph",
        usage="heph [armory] | heph {init,index,ask,config} ...",
        description="Cited answers from the files in an armory.",
    )
    parser.add_argument("--version", action="store_true", help="print the version and exit")
    commands = parser.add_subparsers(dest="command", metavar="{init,index,ask,config}")
    init = commands.add_parser("init", help="make this folder (or a name or path) an armory")
    init.add_argument("target", nargs="?", metavar="name|path")
    index = commands.add_parser("index", help="index an armory's materials")
    index.add_argument("armory", nargs="?")
    question = commands.add_parser("ask", help="answer one question with citations")
    question.add_argument("armory")
    question.add_argument("question", nargs="+")
    question.add_argument("--json", action="store_true", help="print a JSON result")
    commands.add_parser("config", help="show the settings in effect")
    repl = commands.add_parser("repl")  # no help=: keeps it out of the command list
    repl.add_argument("armory", nargs="?")
    return parser


class _Quiet:
    """Event sink for --json: the result is printed once, at the end."""

    def reasoning(self, text: str) -> None:
        del text

    def block(self, text: str, offset: int, citations: Sequence[Citation]) -> None:
        del text, offset, citations


def _index(root: Path, renderer: Renderer) -> Index:
    with renderer.status("Indexing"):
        index, report = build(
            root, lambda n, total, source: renderer.update(f"Indexing {n}/{total} {source}")
        )
    renderer.report(report)
    return index


def _engine(config: Config, index: Index) -> Engine:
    login, model = logins.active(logins.load())
    client = logins.client(login)
    if not model:
        models = client.models()
        if not models:
            raise HephError(f"{login.name} lists no models; pick one with /model")
        model = models[0]
    return Engine(client, model, config, index)


def _count(models: list[str]) -> str:
    return f"{len(models)} model{'' if len(models) == 1 else 's'}"


def _turn(engine: Engine, chat: Chat, question: str, renderer: Renderer) -> Result:
    with renderer.status(f"Waiting for {engine.model}"):
        result = ask(engine, chat.turns, question, renderer)
    renderer.finish(result)
    chat.add(result)
    return result


@dataclass(slots=True)
class _Open:
    """The armory a session works in."""

    root: Path
    index: Index
    chat: Chat
    engine: Engine | None = None
    last: Result | None = None


def _history(root: Path) -> Path:
    path = root / armory.INTERNAL / "history"
    if path.is_symlink():
        raise HephError(f"Refusing symlinked history file {path}")
    return path


class _Repl:
    def __init__(self, out: Renderer) -> None:
        self.out = out
        self.config = load()
        self.saved = logins.load()
        self.catalog: dict[str, list[str]] = {}  # login name -> its models, from /model
        self.open: _Open | None = None
        choices = {
            "armory": lambda: [p.name for p in armory.known()],
            "model": self.choices,
            "login": lambda: list(logins.PROVIDERS),
            "logout": lambda: [item.name for item in self.saved.items],
        }
        self.input = prompt.Input(choices, self.status)

    def current(self) -> _Open:
        if self.open is None:
            raise HephError("No armory open: /armory lists them, /init creates one")
        return self.open

    def status(self) -> str:
        """The line under the prompt: armory, chunks, model, login."""
        login, model = logins.active(self.saved)
        if self.open is None:
            parts = ("no armory", model, login.name, "/armory", "/init", "/help")
        else:
            engine = self.open.engine
            chunks = f"{len(self.open.index.chunks)} chunks"
            model = engine.model if engine else model
            parts = (self.open.root.name, chunks, model, login.name, "/help")
        return SEP.join(part for part in parts if part)

    def all_logins(self) -> list[logins.Login]:
        """The saved logins, plus the one in use when it isn't saved (default or HEPH_BASE_URL)."""
        using = logins.active(self.saved)[0]
        saved = list(self.saved.items)
        return saved if using in saved else [*saved, using]

    def choices(self) -> list[str]:
        """Completions for /model: login names, then login/model pairs seen by /model."""
        names = [login.name for login in self.all_logins()]
        return [*names, *(f"{name}/{m}" for name, models in self.catalog.items() for m in models)]

    def scan(self) -> dict[str, str]:
        """Asks every login for its models; answers the logins that failed, with why."""
        problems: dict[str, str] = {}
        with self.out.status("Checking logins"):
            for login in self.all_logins():
                try:
                    self.catalog[login.name] = logins.client(login).models()
                except HephError as exc:
                    self.catalog[login.name] = []
                    problems[login.name] = str(exc)
        return problems

    def use(self, name: str, model: str) -> None:
        """Makes a login and model the active pair, saved for next time."""
        login = next(item for item in self.all_logins() if item.name == name)
        if login.name == "HEPH_BASE_URL":
            raise HephError("HEPH_BASE_URL is set, so it decides the model; unset it to switch")
        self.saved = replace(logins.add(self.saved, login), login=name, model=model)
        logins.save(self.saved)
        if self.open is not None:
            self.open.engine = None
        known = self.catalog.get(name, [])
        shown = model or (f"{known[0]} (its first model)" if known else "its first model")
        self.out.note(f"Using {shown} on {name}.")

    def models(self) -> None:
        problems = self.scan()
        login, model = logins.active(self.saved)
        width = max(len(item.name) for item in self.all_logins())
        for item in self.all_logins():
            found = self.catalog.get(item.name, [])
            if item.name in problems:
                detail = problems[item.name].split(". ")[0]
            elif item == login:
                detail = f"{model or (found[0] if found else '')}   (active, {_count(found)})"
            else:
                detail = SEP.join(found[:3]) + (f"   +{len(found) - 3} more" if found[3:] else "")
            style = "red" if item.name in problems else ""
            self.out.note(f"  {item.name.ljust(width)}{SEP}{detail}", style)
        self.out.note(
            "Switch: /model <name>   add a local server: /model http://host:port/v1   "
            "log in: /login"
        )

    def switch(self, query: str) -> None:
        if query.startswith(("http://", "https://")):
            self.connect(logins.url_login(query))
            return
        if not self.catalog:
            _ = self.scan()
        matches = rank(query, self.choices())
        if not matches:
            raise HephError(f"No login or model like {query!r}; /model lists them")
        name, _, model = matches[0].partition("/")
        self.use(name, model)

    def connect(self, login: logins.Login) -> None:
        """Adds a local server, asking for a key when it wants one, and switches to it."""
        try:
            found = logins.client(login).models()
        except AuthError:
            key = self.input.secret(f"{login.name} wants an API key: ").strip()
            if not key:
                raise HephError(f"No key given; {login.name} not added") from None
            logins.write_private(logins.key_path(login.name), key + "\n")
            found = logins.client(login).models()
        except HephError as exc:
            self.out.note(f"Saved, but {login.name} did not answer: {exc}", "yellow")
            found = []
        self.catalog[login.name] = found
        self.saved = logins.add(self.saved, login)
        self.use(login.name, found[0] if found else "")

    def login(self, arg: str) -> None:
        provider, _, rest = arg.partition(" ")
        if not provider:
            width = max(map(len, logins.PROVIDERS))
            for name, info in logins.PROVIDERS.items():
                self.out.note(f"  {name.ljust(width)}{SEP}{info.title}", "")
            self.out.note("Log in: /login <provider>   a local server: /login local <url>")
            return
        matches = rank(provider, list(logins.PROVIDERS))
        if not matches:
            raise HephError(f"No provider like {provider!r}; /login lists them")
        name = matches[0]
        info = logins.PROVIDERS[name]
        if name == "local":
            self.connect(logins.url_login(rest))
        elif name == logins.CODEX:
            codex.login(logins.key_path(name), self.out.note, self.input.ask)
            self.saved = logins.add(self.saved, logins.Login(name, name, "", ""))
            self.catalog[name] = list(codex.MODELS)
            self.use(name, codex.MODELS[0])
        else:
            key = self.input.secret(f"{info.title} API key (Enter: use ${info.key_env}): ").strip()
            if key:
                logins.write_private(logins.key_path(name), key + "\n")
            elif not os.environ.get(info.key_env):
                raise HephError(f"No key given and ${info.key_env} is not set; not logged in")
            login = logins.Login(name, name, info.base_url, "")
            found = logins.client(login).models()
            self.catalog[name] = found
            self.saved = logins.add(self.saved, login)
            logins.save(self.saved)
            self.out.note(f"Logged in to {info.title}: {_count(found)}. Pick: /model {name}/")

    def logout(self, arg: str) -> None:
        matches = rank(arg, [item.name for item in self.saved.items])
        if not arg or not matches:
            raise HephError("Usage: /logout <login>; /model lists your logins")
        self.saved = logins.remove(self.saved, matches[0])
        logins.save(self.saved)
        _ = self.catalog.pop(matches[0], None)
        if self.open is not None:
            self.open.engine = None
        self.out.note(f"Logged out of {matches[0]}.")

    def enter(self, root: Path) -> None:
        """Opens an armory: indexes it and switches to its input history."""
        index = _index(root, self.out)
        self.input.history(_history(root))
        self.open = _Open(root, index, Chat.new(root))

    def armories(self) -> list[Path]:
        roots = armory.known()
        if not roots:
            self.out.note(f"No armories in {armory.armory_home()} yet.")
        for number, root in enumerate(roots, 1):
            is_open = self.open is not None and self.open.root == root.resolve()
            self.out.note(f"  {number}  {root.name}{'  (open)' if is_open else ''}", "")
        return roots

    def welcome(self) -> None:
        cwd = Path.cwd()
        pick = "Type a number or name to open one. " if self.armories() else ""
        home = armory.armory_home()
        if armory.refused(cwd):
            create = f"/init <name> creates {home}/<name>; `heph init` in a folder makes it one."
        else:
            create = f"/init makes {cwd} an armory; /init <name> creates {home}/<name>."
        self.out.note(pick + create)

    def pick(self, choice: str) -> None:
        if choice.isdigit():
            roots = armory.known()
            number = int(choice)
            if not 1 <= number <= len(roots):
                raise HephError(f"No armory number {number}; /armory lists them")
            self.enter(armory.validate(roots[number - 1]))
        else:
            self.enter(armory.resolve(choice))

    def create(self, target: str | None) -> None:
        if target is None and (cwd := armory.here()) is not None:
            self.out.note(f"{cwd} is already an armory.")
            self.enter(cwd)
            return
        root = armory.init(target)
        self.out.note(f"Created armory {root}.")
        self.enter(root)
        if not self.current().index.chunks:
            self.out.note(f"Add files with /add <path>, or copy them into {root}.")

    def reindex(self, current: _Open) -> None:
        current.index, current.engine = _index(current.root, self.out), None

    def add(self, arg: str) -> None:
        current = self.current()
        try:
            paths = shlex.split(arg)
        except ValueError as exc:
            raise HephError(f"Cannot parse {arg!r}: {exc}") from exc
        if not paths:
            raise HephError("Usage: /add <file or folder> ... (quote paths with spaces)")
        for count, raw in enumerate(paths):
            try:
                name = armory.add(current.root, Path(raw).expanduser())
            except HephError:
                if count:
                    self.reindex(current)
                raise
            self.out.note(f"Added {name}")
        self.reindex(current)

    def model(self, arg: str) -> None:
        if arg:
            self.switch(arg)
        else:
            self.models()

    def armory(self, arg: str) -> None:
        if arg:
            self.pick(arg)
        else:
            _ = self.armories()

    def new(self, _arg: str) -> None:
        current = self.current()
        current.chat, current.last = Chat.new(current.root), None
        self.out.note("New chat.")

    def sources(self, _arg: str) -> None:
        last = self.current().last
        self.out.sources(last.evidence if last else ())

    def command(self, line: str) -> bool:
        """Handles a slash command, forgiving typos in its name; returns False to quit."""
        name, _, rest = line[1:].partition(" ")
        found = prompt.command(name)
        if found == "exit":
            return False
        handlers: dict[str, Callable[[str], None]] = {
            "help": lambda _: self.out.note(prompt.help_text(), style=""),
            "model": self.model,
            "login": self.login,
            "logout": self.logout,
            "armory": self.armory,
            "init": lambda arg: self.create(arg or None),
            "add": self.add,
            "new": self.new,
            "sources": self.sources,
            "index": lambda _: self.reindex(self.current()),
        }
        if found is None or found not in handlers:
            self.out.note(f"No command like /{name}; /help lists commands.", "yellow")
        else:
            handlers[found](rest.strip())
        return True

    def question(self, line: str) -> None:
        current = self.current()
        current.engine = current.engine or _engine(self.config, current.index)
        current.last = _turn(current.engine, current.chat, line, self.out)

    def loop(self) -> None:
        while True:
            try:
                line = self.input.read().strip()
            except KeyboardInterrupt:
                self.out.console.print()
                continue
            except EOFError:
                self.out.console.print()
                return
            try:
                if line.startswith("/"):
                    if not self.command(line):
                        return
                elif line and self.open is None:
                    self.pick(line)
                elif line:
                    self.question(line)
            except HephError as exc:
                self.out.note(f"error: {exc}", "red")
            except KeyboardInterrupt:
                self.out.note("interrupted", "yellow")


def _repl(arg: str | None, out: Renderer) -> None:
    repl = _Repl(out)
    out.fullscreen()
    if arg is not None:
        repl.enter(armory.resolve(arg))
    elif (cwd := armory.here()) is not None:
        repl.enter(cwd)
    else:
        repl.welcome()
    repl.loop()


def _run(args: Args, out: Renderer, err: Renderer) -> None:
    match args.command:
        case "init":
            root = armory.init(args.target)
            run = "heph" if args.target is None else f"heph {args.target}"
            out.note(f"Created armory {root}. Every file in it is material.", "")
            out.note(f"Run `{run}` to ask questions; add files any time.", "")
        case "index":
            _index(armory.resolve(args.armory), out)
        case "ask":
            root = armory.resolve(args.armory)
            question = " ".join(args.question)
            engine = _engine(load(), _index(root, err))
            if args.json:
                result = ask(engine, [], question, _Quiet())
                Chat.new(root).add(result)
                out.console.file.write(json.dumps(result.json(), ensure_ascii=False, indent=2))
                out.console.file.write("\n")
            else:
                _turn(engine, Chat.new(root), question, out)
        case "config":
            config = load()
            state = "" if config.path.exists() else " (not created; defaults in effect)"
            out.note(f"settings: {config.path}{state}", "")
            for key, value in (
                ("max_tokens", config.max_tokens),
                ("temperature", config.temperature),
                ("evidence_tokens", config.evidence_tokens),
            ):
                out.note(f"  {key} = {value}", "")
            saved = logins.load()
            login, model = logins.active(saved)
            out.note(f"logins: {saved.path}", "")
            for item in saved.items or (login,):
                title = logins.PROVIDERS[item.provider].title
                where = item.base_url or title
                using = f"{SEP}(active, {model or 'first listed model'})" if item == login else ""
                out.note(f"  {item.name}{SEP}{where}{using}", "")
        case _:
            _repl(args.armory, out)


def main(argv: Sequence[str] | None = None) -> int:
    words = list(sys.argv[1:] if argv is None else argv)
    if not words or (words[0] not in _COMMANDS and not words[0].startswith("-")):
        words.insert(0, "repl")
    args = _parser().parse_args(words, namespace=Args())
    if args.version:
        print(f"heph {version('heph')}")
        return 0
    if args.command is None:
        args.armory = None
    err = Renderer(console(stderr=True))
    try:
        _run(args, Renderer(console()), err)
    except HephError as exc:
        err.console.print(Text(f"heph: {exc}", style="red"))
        return 1
    except KeyboardInterrupt:
        return 130
    return 0
