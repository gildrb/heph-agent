# Getting started

Heph answers questions from the files in an armory. Every answer cites its evidence, and
Heph checks each quoted citation against the evidence it came from.

## Install

Heph needs Python 3.13+ on Linux or macOS.

```bash
uv tool install heph    # or: pip install heph
heph --version
```

Wheels ship a prebuilt `heph-core` binary for Linux x86_64 and arm64 (manylinux) and
macOS arm64. On other platforms pip builds it from the source distribution, which needs
clang 14 or newer. Heph downloads nothing at install or at run time.

## Start a model server

Heph talks to any OpenAI-compatible server. By default it expects one on your machine at
`http://127.0.0.1:8080/v1`, for example llama.cpp:

```bash
llama-server -m ~/models/your-model.gguf --port 8080
```

vLLM, SGLang and Ollama work too: point `base_url` at them (see
[Configuration](configuration.md)). Local servers need no API key. Heph never starts,
installs or downloads servers or models; you run them yourself.

## Create an armory

```bash
heph init notes                      # creates ~/.armories/notes
cp ~/Documents/report.pdf ~/.armories/notes/materials/
heph index notes                     # optional: heph also indexes before answering
```

Supported materials: PDF, DOCX, PPTX, XLSX, ODT, ODS, and UTF-8 text (Markdown, notes,
code). See [Armories](armories.md).

## Ask

```bash
heph notes                           # interactive session in the armory
heph ask notes "What does the report conclude?"
heph ask notes "What does the report conclude?" --json
```

In a session, type a question and press Enter. The answer streams into the terminal.
Each citation is colored once it has been checked: green for a verified quote, yellow for
a citation without a quote, red for a quote that does not match or an unknown evidence id.
A sources footer lists every citation, and a stats line shows the model, tokens, tok/s
and time.

Commands in a session:

| Command | Action |
| --- | --- |
| `/help` | list commands |
| `/new` | start a new chat |
| `/sources` | show the evidence of the last answer |
| `/index` | re-index the armory |
| `/exit` | quit (Ctrl-D works too) |

## CLI

| Command | Action |
| --- | --- |
| `heph [armory]` | interactive session (armory name, path, or the current directory) |
| `heph init <name\|path>` | create an armory |
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
