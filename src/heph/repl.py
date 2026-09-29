"""The interactive session: ask right away; switch armories and models from pickers."""

import os
import shlex
from collections.abc import Callable
from dataclasses import dataclass, replace
from pathlib import Path

from heph import HephError, armory, codex, logins, prompt
from heph.answer import Engine, Result, ask
from heph.config import Config, load
from heph.fuzzy import rank
from heph.index import Index, build
from heph.llm import AuthError
from heph.prompt import Option, pick
from heph.render import SEP, Renderer
from heph.session import Chat

_ADD_SERVER = "+server"
_LOG_IN = "+login"


def index_armory(root: Path, renderer: Renderer, *, quiet: bool) -> Index:
    with renderer.status("Indexing"):
        index, report = build(
            root, lambda n, total, source: renderer.update(f"Indexing {n}/{total} {source}")
        )
    renderer.report(report, quiet=quiet)
    return index


def engine_for(config: Config, index: Index) -> Engine:
    login, model = logins.active(logins.load())
    client = logins.client(login)
    if not model:
        models = client.models()
        if not models:
            raise HephError(f"{login.name} lists no models; pick one with /model")
        model = models[0]
    return Engine(client, model, config, index)


def turn(engine: Engine, chat: Chat, question: str, renderer: Renderer) -> Result:
    with renderer.status(f"Waiting for {engine.model}"):
        result = ask(engine, chat.turns, question, renderer)
    renderer.finish(result)
    chat.add(result)
    return result


def _plural(count: int, word: str) -> str:
    return f"{count} {word}{'' if count == 1 else 's'}"


@dataclass(slots=True)
class _Open:
    """The armory a session works in."""

    root: Path
    index: Index
    files: int
    snapshot: tuple[tuple[str, int, int], ...]
    chat: Chat
    engine: Engine | None = None
    last: Result | None = None


def _history(root: Path) -> Path:
    path = root / armory.INTERNAL / "history"
    if path.is_symlink():
        raise HephError(f"Refusing symlinked history file {path}")
    return path


class Repl:
    def __init__(self, out: Renderer, root: Path) -> None:
        self.out = out
        self.config = load()
        self.saved = logins.load()
        self.catalog: dict[str, list[str]] = {}  # login name -> its models, from /model
        choices: dict[str, Callable[[], list[str]]] = {
            "armory": lambda: [p.name for p in armory.recent()],
            "model": self.choices,
            "logout": lambda: [item.name for item in self.saved.items],
        }
        self.input = prompt.Input(choices, self.status)
        self.open = self._open(root, Chat.new(root))
        self.input.history(_history(root))
        self._hint()

    # Armories

    def _open(self, root: Path, chat: Chat) -> _Open:
        snapshot = armory.snapshot(root)
        index = index_armory(root, self.out, quiet=True)
        files = len({chunk.source for chunk in index.chunks})
        return _Open(root, index, files, snapshot, chat)

    def _hint(self) -> None:
        root = self.open.root
        if root.name == armory.GUIDE:
            self.out.note("This is the Heph guide. Ask it anything: how do I add my own files?")
        elif not self.open.files:
            self.out.note(f"{root.name} is empty: /add files, or copy them into {root}.")

    def enter(self, root: Path) -> None:
        self.open = self._open(root, Chat.new(root))
        self.input.history(_history(root))
        self.out.note(f"Opened {root.name}.")
        self._hint()

    def status(self) -> str:
        """The line at the bottom: armory, files, model, login."""
        login, model = logins.active(self.saved)
        engine = self.open.engine
        files = _plural(self.open.files, "file")
        parts = (self.open.root.name, files, engine.model if engine else model, login.name)
        return SEP.join(part for part in (*parts, "/ for commands") if part)

    def armory(self, arg: str) -> None:
        if arg:
            self.enter(self._named(arg))
            return
        roots = armory.recent()
        options = [
            Option(str(root), root.name, "open now" if root == self.open.root else str(root))
            for root in roots
        ]
        if armory.GUIDE not in {root.name for root in roots}:
            options.append(Option("guide", armory.GUIDE, "how to use Heph"))
        cwd = Path.cwd()
        if armory.here() is None and not armory.refused(cwd):
            options.append(Option("here", "use this folder", str(cwd)))
        options.append(Option("new", "new armory", f"a new folder in {armory.armory_home()}"))
        match pick("Open an armory", options):
            case None:
                return
            case "guide":
                self.enter(armory.guide())
            case "here":
                self.enter(armory.init(None))
            case "new":
                name = self.input.ask("Name: ").strip()
                if name:
                    self.enter(armory.init(name))
            case path:
                self.enter(armory.validate(Path(path)))

    def _named(self, arg: str) -> Path:
        if arg.isdigit():
            roots = armory.recent()
            if not 1 <= int(arg) <= len(roots):
                raise HephError(f"No armory number {arg}; /armory lists them")
            return armory.validate(roots[int(arg) - 1])
        if arg.casefold() in {"guide", armory.GUIDE}:
            return armory.guide()
        return armory.resolve(arg)

    def add(self, arg: str) -> None:
        raw = arg or self.input.path("File or folder to add (drag it here): ")
        try:
            paths = shlex.split(raw)
        except ValueError as exc:
            raise HephError(f"Cannot parse {raw!r}: {exc}") from exc
        added = 0
        try:
            for path in paths:
                self.out.note(f"Added {armory.add(self.open.root, Path(path).expanduser())}")
                added += 1
        finally:
            if added:
                self.reindex()

    def reindex(self) -> None:
        current = self.open
        self.open = replace(self._open(current.root, current.chat), last=current.last)

    # Models and logins

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
                    problems[login.name] = str(exc).split(". ")[0]
        return problems

    def use(self, name: str, model: str) -> None:
        """Makes a login and model the active pair, saved for next time."""
        login = next(item for item in self.all_logins() if item.name == name)
        if login.name == "HEPH_BASE_URL":
            raise HephError("HEPH_BASE_URL is set, so it decides the model; unset it to switch")
        self.saved = replace(logins.add(self.saved, login), login=name, model=model)
        logins.save(self.saved)
        self.open.engine = None
        known = self.catalog.get(name, [])
        self.out.note(f"Using {model or (known[0] if known else 'the first model')} on {name}.")

    def model(self, arg: str) -> None:
        if arg:
            self.switch(arg)
            return
        problems = self.scan()
        login, model = logins.active(self.saved)
        using = model or next(iter(self.catalog.get(login.name, [])), "")
        options: list[Option] = []
        for item in self.all_logins():
            if item.name in problems:
                options.append(Option(f"{item.name}/", item.name, problems[item.name]))
            for name in self.catalog.get(item.name, []):
                option = Option(f"{item.name}/{name}", name, item.name)
                if item == login and name == using:
                    options.insert(0, replace(option, detail=f"{item.name}   in use"))
                else:
                    options.append(option)
        options.append(Option(_ADD_SERVER, "add a local server", "any OpenAI-compatible URL"))
        options.append(Option(_LOG_IN, "log in", "OpenAI, OpenRouter, DeepSeek, Z.AI, ChatGPT"))
        match pick("Pick a model", options):
            case None:
                return
            case str(choice) if choice == _ADD_SERVER:
                self.server("")
            case str(choice) if choice == _LOG_IN:
                self.login("")
            case choice:
                name, _, chosen = choice.partition("/")
                self.use(name, chosen)

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

    def server(self, url: str) -> None:
        url = url or self.input.ask("Server URL: ", default=logins.DEFAULT_URL).strip()
        if url:
            self.connect(logins.url_login(url))

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
        if provider:
            matches = rank(provider, list(logins.PROVIDERS))
            if not matches:
                raise HephError(f"No provider like {provider!r}; /login lists them")
            name = matches[0]
        else:
            options = [Option(slug, info.title) for slug, info in logins.PROVIDERS.items()]
            chosen = pick("Log in to", options)
            if chosen is None:
                return
            name = chosen
        info = logins.PROVIDERS[name]
        if name == "local":
            self.server(rest)
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
            self.catalog[name] = logins.client(login).models()
            self.saved = logins.add(self.saved, login)
            logins.save(self.saved)
            found = _plural(len(self.catalog[name]), "model")
            self.out.note(f"Logged in to {info.title}: {found}. /model to pick one.")

    def logout(self, arg: str) -> None:
        names = [item.name for item in self.saved.items]
        if arg:
            matches = rank(arg, names)
            if not matches:
                raise HephError(f"No login like {arg!r}; /model lists them")
            name: str | None = matches[0]
        else:
            name = pick("Log out of", [Option(item, item) for item in names])
        if name is None:
            return
        self.saved = logins.remove(self.saved, name)
        logins.save(self.saved)
        _ = self.catalog.pop(name, None)
        self.open.engine = None
        self.out.note(f"Logged out of {name}.")

    # Chat

    def new(self, _arg: str) -> None:
        self.open.chat, self.open.last = Chat.new(self.open.root), None
        self.out.note("New chat.")

    def sources(self, _arg: str) -> None:
        last = self.open.last
        self.out.sources(last.evidence if last else ())

    def question(self, line: str) -> None:
        if armory.snapshot(self.open.root) != self.open.snapshot:
            self.reindex()
        current = self.open
        current.engine = current.engine or engine_for(self.config, current.index)
        current.last = turn(current.engine, current.chat, line, self.out)

    def command(self, line: str) -> bool:
        """Handles a slash command, forgiving typos in its name; returns False to quit."""
        name, _, rest = line[1:].partition(" ")
        found = prompt.command(name)
        if found == "exit":
            return False
        handlers: dict[str, Callable[[str], None]] = {
            "help": lambda _: self.out.note(prompt.help_text(), style=""),
            "model": self.model,
            "armory": self.armory,
            "add": self.add,
            "new": self.new,
            "sources": self.sources,
            "login": self.login,
            "logout": self.logout,
        }
        if found is None or found not in handlers:
            self.out.note(f"No command like /{name}; type / to see them.", "yellow")
        else:
            handlers[found](rest.strip())
        return True

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
                elif line:
                    self.question(line)
            except HephError as exc:
                self.out.note(f"error: {exc}", "red")
            except KeyboardInterrupt:
                self.out.note("interrupted", "yellow")


def run(arg: str | None, out: Renderer) -> None:
    """Opens the armory named, else the one here, else the Heph guide."""
    out.fullscreen()
    if arg is not None:
        root = armory.resolve(arg)
    elif (here := armory.here()) is not None:
        root = here
    else:
        root = armory.guide()
    Repl(out, root).loop()
