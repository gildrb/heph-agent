# Getting started

Heph answers questions from the files in an armory. Every answer cites its evidence, and
Heph checks each quoted citation against the evidence it came from.

## Install

Heph needs Python 3.14 and clang 14+ on Linux or macOS: the first build compiles its core
from `core/build/heph-core.c`.

```bash
git clone https://github.com/gildrb/heph-agent
cd heph-agent
uv sync
uv run heph --version
```

Or `brew install gildrb/heph/heph`. Beyond the source and its Python dependencies, Heph
downloads nothing at install or at run time.

## Pick a model

Heph talks to any OpenAI-compatible server. With no login it expects one on your machine
at `http://127.0.0.1:8080/v1`, for example llama.cpp:

```bash
llama-server -m ~/models/your-model.gguf --port 8080
```

Inside `heph`, `/model` lists your logins and their models, `/model http://host:port/v1`
adds any local server (vLLM, SGLang, Ollama...), and `/login` adds OpenAI, OpenRouter,
DeepSeek, Z.AI or your ChatGPT subscription. See [Configuration](configuration.md). Heph
never starts, installs or downloads servers or models; you run them yourself.

## Create an armory

Any folder of documents becomes an armory:

```bash
cd ~/Documents/contracts             # a folder with your files
heph init                            # make it an armory
heph                                 # ask questions
```

Or stay inside Heph: `/armory` picks "new armory" or "use this folder", and `/add` asks for
a file or folder to copy in (or `/add ~/Downloads/report.pdf`).

Supported materials: PDF, DOCX, PPTX, XLSX, ODT, ODS, and UTF-8 text (Markdown, notes,
code). See [Armories](armories.md).

## Ask

```bash
heph                                 # the armory in this folder, or the Heph guide
heph notes                           # the armory ~/.armories/notes
heph ask notes "What does the report conclude?"
heph ask notes "What does the report conclude?" --json
```

In a session, type a question and press Enter. Heph echoes it, then shows:

| Line | Means |
| --- | --- |
| `Read 5 passages   a.md  b.pdf` | the files whose passages were sent to the model |
| `Esc  Thinking…  4s` | working; the answer streams in above this line; Esc stops it |
| `Thought for 6.2s` | time from the request to the first word of the answer |
| the answer | citations colored once checked: green verified, yellow no quote, red not found |
| `✓ E1  a.md   2 quotes verified` | one line per cited passage |
| `qwen3.8-27b low   in 2.3K   out 1.7K   9.9s   174 tok/s` | model, reasoning level, tokens, time, speed |

Under the prompt, the status bar shows the model and its reasoning level, the armory and its
file count, and the login on the right. New, changed and deleted files are re-indexed before
the next question.

Outside an armory, `heph` opens the Heph guide (`~/.armories/heph-guide`): pages about
Heph itself, refreshed from the installed version. Ask it how Heph works.

Type `/` for the command menu; it narrows as you type. Commands are forgiving: `/mod`,
`/mdl` and `/modle` all run `/model`. Commands without an argument open a picker: type to
filter, arrows move, Enter picks, Esc skips. In the `/` menu, Tab completes and Enter runs.

| Key | Action |
| --- | --- |
| Esc | stop an answer; what you typed meanwhile stays in the prompt |
| Ctrl+C | clear the line; twice on an empty line to quit |
| Ctrl+D | quit |
| Ctrl+L | pick a model |
| Ctrl+O | show the passages behind the last answer, in full |
| Ctrl+T | show or hide the model's thinking after each answer |
| Shift+Tab | next reasoning level of the model in use (see [Configuration](configuration.md#reasoning-levels)) |
| Alt+Enter | new line |
| Up, Down | earlier questions |

| Command | Action |
| --- | --- |
| `/model [name\|url]` | pick a model from every login, add a local server, or log in |
| `/armory [name]` | pick an armory, use this folder, or create one in `~/.armories` |
| `/add [path]...` | copy files or folders into the armory (asks when no path), then index |
| `/login [provider]` | add a login: OpenAI, OpenRouter, DeepSeek, Z.AI, Codex, local |
| `/logout [login]` | remove a login and its saved key |
| `/new` | start a new chat |
| `/sources` | show the passages behind the last answer (Ctrl+O) |
| `/help` | list commands and keys |
| `/exit` | quit (Ctrl+D works too) |

## CLI

| Command | Action |
| --- | --- |
| `heph [armory]` | interactive session: the armory named, the current folder, or the Heph guide |
| `heph init [name\|path]` | make the current folder (or a name or path) an armory |
| `heph index [armory]` | update the index |
| `heph ask <armory> <question...> [--json]` | answer one question |
| `heph config` | show the settings in use and the config file path |
| `heph --version` | print the version |

## Read next

- [Configuration](configuration.md)
- [Armories](armories.md)
- [Privacy](privacy.md)
- [Troubleshooting](troubleshooting.md)
- [Architecture](architecture.md)
