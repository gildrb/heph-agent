<p align="center">
  <img alt="Heph" src="assets/logo-auto.svg" width="280">
</p>

<p align="center">
  Local agent for accurate, cited answers from your files
</p>

<p align="center">
  <a href="https://raw.githubusercontent.com/gildrb/heph-agent/main/assets/app-screenshot.png?v=77d812a64833">
    <img alt="Heph answering a question with a verified citation" src="assets/app-screenshot.png?v=77d812a64833" width="100%">
  </a>
</p>

## Quick Start

```bash
# Install UV (if not already installed)
curl -LsSf https://astral.sh/uv/install.sh | sh

# Install Heph
uv tool install heph@latest

# Make a folder of your files an armory
cd ~/Documents/[folder]
heph init

# Start Heph (it expects a model server at http://127.0.0.1:8080/v1)
heph
```

No folder yet? Run `heph` anywhere, then `/init [name]` and `/add [file]`.

## Compared to Heph 0.0.59

| | 0.0.59 | Now | Proof |
| --- | --- | --- | --- |
| A ✓ citation means | the cited ID exists | the quoted words are in that passage | [`cite_sound`](core/LAWS.bend), proven in CI |
| Model calls per question | 3 or more | 1 | [`answer.py`](src/heph/answer.py) |
| Default model | hosted (Pollinations) | your local server | [Configuration](docs/configuration.md) |
| Downloads or runs | llama.cpp, models, updates, shell, plugins | nothing | [Architecture](docs/architecture.md#zero-remote-code-execution) |
| Install size (Linux, Python 3.14) | 144 packages, 5.8 GiB | 8 packages, 14 MiB | measured in an empty venv |
| Heph's own code | 2.55 MB Python | 67 KB Python + 46 KB Bend | [`src/heph`](src/heph), [`core`](core) |
| Interface | full-screen TUI | inline CLI | screenshot above |

## Armory layout

An armory is a normal folder. Every visible file in it is material:

```text
~/Documents/[folder]/
├── [file].pdf            # PDFs, Office docs, notes, code to cite
├── [subfolder]/          # Subfolders count too
│   └── [file].md
├── .harnessignore        # Optional: files to skip (gitignore-style)
└── .harness/             # Local Heph state
    ├── armory.toml       # Armory marker
    ├── index/            # Retrieval index, one cache per file
    ├── chats/            # Saved chats
    └── history           # Input history
```

| Command | Armory |
| --- | --- |
| `heph init` | the current folder |
| `heph init [name]` | `~/.armories/[name]` |
| `heph` | the current folder, or a list to pick from |
| `heph [name]` | `~/.armories/[name]` |

Copy or sync the folder to move it between machines; configure the model server again on
each machine.

Read [Armories](docs/armories.md) for storage, indexing, and ignore rules.

## Installation

> [!NOTE]
> Heph is currently in beta, so unexpected issues may occur. Please report them if
> they have not already been reported.

### Using UV (recommended)

Install UV:

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

Then Heph:

```bash
uv tool install heph@latest
```

### Using Pip

```bash
pip install heph
```

### Using mise

```bash
mise use -g pypi:heph
```

Heph needs Python 3.14+ on Linux or macOS.

| Reads | How |
| --- | --- |
| PDF | text per page (bundled PDFium); citations show the page |
| DOCX, PPTX, XLSX, ODT, ODS | built-in XML parsing |
| Markdown, notes, code, CSV | UTF-8 text |

Convert `.doc`, `.ppt`, `.xls`, `.odp`, and `.rtf` to PDF, DOCX, PPTX or XLSX first.
Scanned PDFs need OCR first.

### Model server

Heph talks to any OpenAI-compatible server. Local servers need no key.

| Server | `base_url` |
| --- | --- |
| llama.cpp `llama-server` | `http://127.0.0.1:8080/v1` (default) |
| vLLM | `http://127.0.0.1:8000/v1` |
| SGLang | `http://127.0.0.1:30000/v1` |
| Ollama | `http://127.0.0.1:11434/v1` |

Set it in `~/.config/heph/config.toml` or with `HEPH_BASE_URL`; `heph config` shows the
settings in use. See [Configuration](docs/configuration.md).

### Updating

```bash
uv tool upgrade heph
```

Check the installed version:

```bash
heph --version
```

## Docs

[Getting started](docs/getting-started.md)<br>
[Armories](docs/armories.md)<br>
[Configuration](docs/configuration.md)<br>
[Privacy](docs/privacy.md)<br>
[Architecture](docs/architecture.md)<br>
[Troubleshooting](docs/troubleshooting.md)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for local development, the proof gate, and pull
request guidelines.

## Safety

- No telemetry, crash reports, or update checks.
- Beyond the package itself, Heph downloads nothing: no engines, models, plugins, or updates.
- The model gets no tools: no shell, no files, no web.
- The only network peer is your `base_url`; the only subprocess is Heph's own core.
