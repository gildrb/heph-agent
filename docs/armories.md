# Armories

An armory is a folder Heph answers from. Every visible file in it, subfolders included, is
material. Heph keeps its own state in `.harness/`.

```text
~/Documents/contracts/        # any folder
├── lease.pdf
├── notes/
│   └── call.md
├── .harnessignore            # optional: files to skip
└── .harness/                 # Heph state
    ├── armory.toml           # marker: this folder is an armory
    ├── index/                # one cache file per material
    ├── chats/                # saved chats
    └── history               # input history
```

| Make an armory | Where it lives |
| --- | --- |
| `heph init` | the current folder |
| `heph init <name>` | `$HEPH_ARMORY_HOME/<name>` (default `~/.armories/<name>`) |
| `heph init <path>` | that path (anything with a `/`, or starting with `.` or `~`) |
| `/init`, `/init <name>` | the same, inside `heph` |

Heph refuses to make your home folder or `/` an armory.

| Open an armory | What happens |
| --- | --- |
| `heph` in an armory | opens it |
| `heph` anywhere else | lists the armories in `~/.armories`; type a number or name |
| `heph <name\|path>` | opens that armory |
| `/armory [name]` | lists armories, or switches to one, inside `heph` |

Add files by copying them into the folder, or with `/add <path>...` inside `heph`, which
copies files or folders in (never overwrites) and re-indexes. Copy or sync the folder to
move an armory; it holds no absolute paths.

## Materials

Heph reads every visible file in the armory except ignored ones:

| Kind | Handling |
| --- | --- |
| PDF | text per page (PDFium); citations show the page |
| DOCX, PPTX, XLSX, ODT, ODS | text from the document XML |
| anything else | UTF-8 text (Markdown, notes, code, CSV...) |

Rejected with a reason: `.doc`, `.ppt`, `.xls`, `.odp`, `.rtf` (save as PDF, DOCX, PPTX or
XLSX), binary files, text that isn't UTF-8, files over 50 MB, text files over 5 MB, and
Office archives that are too large, have too many members or contain traversal paths.

Materials are untrusted input. Heph never follows symlinks inside the armory, never runs or
loads anything from it, and parses XML with `defusedxml`.

## Ignore rules

`.harnessignore` at the armory root uses a subset of gitignore syntax, one pattern per
line:

```text
# lines starting with # are comments
drafts/
*.log
notes/old/*.md
```

- `name/` matches directories only, at any depth.
- A pattern without an inner `/` is a glob matched against file and directory names.
- A pattern with an inner `/` is matched against the whole path from the armory root.
- `!` negation and trailing comments are not supported.

`.git/`, `__pycache__/`, `node_modules/`, `.DS_Store`, hidden files and directories
(including `.harness/`), and symlinks are always skipped.

## Index

`heph index` (and every question) brings the index up to date. Each material gets one
cache file, `.harness/index/<sha256 of the file bytes>.json`, holding its text, page
offsets, chunks and per-chunk term counts. Unchanged files are not re-read; caches of
removed or changed files are deleted. Deleting `.harness/index/` is always safe.

Chunks are about 1200 bytes with 200 bytes of overlap, cut at paragraph breaks where
possible. Retrieval is BM25 over those chunks; see [Architecture](architecture.md).

## Chats

Chats are saved in `.harness/chats/`. `/new` starts a new one. Only questions and answers
are kept in the history sent to the model; evidence is retrieved again for each question.
