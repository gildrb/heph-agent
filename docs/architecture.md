# Architecture

Heph is a small Python CLI around a law-checked core written in
[Bend 2](https://bend-lang.com). Python does I/O: files, extraction, the index, the model
server and the terminal. The core does the three computations that decide what a user can
trust — chunking, ranking and citation checking — and each has laws proven by the Bend
checker.

```text
materials ─ extract ─ core: chunk ─ tokenize ─ index cache (.harness/index/)
question ─ tokenize ─ postings ─ core: rank ─ evidence E1..En ─ prompt ─ model (SSE)
answer ─ parse citations ─ core: verify ─ rendered answer + sources + stats
```

## Python (`src/heph/`)

| Module | Role |
| --- | --- |
| `cli.py` | argparse entry point: `init`, `index`, `ask`, `config`, the session |
| `repl.py` | interactive session: slash commands, pickers, keys, re-index on file changes |
| `prompt.py` | input in oh-my-pi's style: borderless editor, slash menu, status bar, pickers, Esc while answering |
| `fuzzy.py` | forgiving name matching: exact, prefix, letters in order, typos |
| `guide/` | the Heph guide armory's pages, copied to `~/.armories/heph-guide` |
| `config.py` | `config.toml` settings |
| `logins.py` | logins (local servers, API keys, Codex), the active model, key files |
| `codex.py` | ChatGPT subscription: OAuth sign-in and the Codex Responses stream |
| `armory.py` | armory layout, init/resolve, the guide, material discovery, ignore rules, safe reads |
| `extract.py` | text from PDF (per page), DOCX, PPTX, XLSX, ODT, ODS and UTF-8 text |
| `core.py` | `heph-core` subprocess client (protocol v1 below) |
| `index.py` | per-file index caches, tokenizer, postings, retrieval |
| `llm.py` | stdlib HTTP client: streaming chat completions, `GET /models` |
| `answer.py` | one turn: retrieve, prompt, stream, verify citations |
| `render.py` | terminal output: shimmering working line, streamed Markdown, checked citations, sources, stats |
| `session.py` | chat persistence in `.harness/chats/` |

Runtime dependencies: `pypdfium2`, `defusedxml`, `rich`, `prompt-toolkit`, `certifi`, all
pinned exactly.

The tokenizer stays in Python because it is Unicode-aware: NFKC normalization and
case-folding, `\w+` runs, dropping one-character non-digit tokens and a short stop list.
Python sends the core postings, not text, for ranking.

## Core (`core/`)

| File | Role |
| --- | --- |
| `core.bend` | chunking, top-k selection, streaming normalization and KMP quote search |
| `score.bend` | the fixed-point BM25 formula and score-table sizing, shared with `LAWS.bend` |
| `main.bend` | stdin frame parser and output writer |
| `stdin.c`, `stdout.c` | the only foreign code: copy stdin into a packed buffer, write a packed buffer to stdout |
| `LAWS.bend` | specifications and law statements (`LAWS-REVIEW.md` compares them with the list-based originals) |
| `PROOF.bend`, `proof/` | proofs of the laws; `bend core/PROOF.bend` must print `ALL PROOFS CHECK` |
| `build.sh` | runs `bend PROOF.bend`, emits `core/build/heph-core.c`, compiles `src/heph/_bin/heph-core` |

Data stays packed end to end: the whole request is one `Array<U32>` (4 bytes per slot, which
Bend 2.0.32 lowers to a contiguous buffer with O(1) reads and writes), documents, evidence
and quotes are offset ranges into it, and scores live in an `Array<Nat>` table. No stage
turns bytes into one-cell-per-byte lists.

The wheel ships `heph/_bin/heph-core`, compiled from that C by `hatch_build.py` (clang,
`-O2`, CPU only). Bend and its checker are needed only to change the core; building a
wheel from the sdist needs only clang.

### Laws

| Law | Statement |
| --- | --- |
| `cite_sound` | a quote judged `ok` at offset `i` equals, byte for byte, the normalized evidence at `i` |
| `cite_badid` | the verdict is `badid` exactly when the evidence id does not exist |
| `rank_scores` | every hit carries its chunk's BM25 score (the sum over query terms of `idf * tfn`), and that score is positive |
| `rank_complete` | a chunk with a positive score is either returned, or `k` hits that all rank strictly before it are |
| `rank_bounded` | ranking returns at most `k` hits |
| `rank_ids_valid` | every returned chunk id is below the chunk count |
| `rank_sorted` | hits are sorted by score descending, then chunk id ascending |
| `chunk_cover` | chunks start at 0, are non-empty, advance, leave no gaps and end at the document end; an empty document has no chunks |
| `chunk_text` | the bytes of every chunk's `[start, end)` in the packed document equal `doc[start:end]` of the document read as a list |

The laws don't cover the protocol layer (`main.bend`) or the two I/O effects, and
`cite_sound` is soundness only: a real quote could still be missed, but a quote marked
verified is really there. `rank_scores` and `rank_complete` assume the score table fits
(`n <= 2^d`, `d <= 31`), which `main.bend` guarantees by rejecting larger requests.

### Semantics

- Chunking: byte offsets, target 1200 bytes, overlap 200; cut at the last `\n\n` past
  600 bytes, else the last ASCII whitespace past 600, else a UTF-8 boundary. A chunk never
  splits a UTF-8 character.
- Ranking: integer fixed-point BM25 (k1 = 6/5, b = 3/4) over the postings Python sends;
  query terms are unique.
- Verification: both sides are normalized (ASCII whitespace runs collapsed to one space,
  ends stripped), then the quote is searched in the evidence.

### Protocol v1

Python runs `[<package>/_bin/heph-core, "--gpu", "off"]` with no shell, writes one
request to stdin and reads the response from stdout. Exit 0 means success; malformed input
exits 2 with a message on stderr. Numbers are ASCII decimal, header lines end with
`\n`, and payloads are length-prefixed raw bytes. Frames of different kinds may be mixed;
each output line carries the 0-based sequence number of its frame, counted per kind.
To keep every integer below the runtime's 2^48 limit, an `R` frame is rejected (exit 2)
when `n >= 2^24`, `tf >= 2^16`, `dl >= 2^14`, `terms >= 2^15` or `total >= 2^40`.

Input frames:

```text
D <len>\n<bytes>                 chunk one UTF-8 document
E <len>\n<bytes>                 add evidence; its id is the number of E frames before it
Q <id> <len>\n<bytes>            verify a quote against evidence <id>
R <k> <n> <total> <terms>\n      rank the top k of n chunks; total = sum of chunk token counts;
                                 then exactly <terms> term blocks:
T <count>\n                      one query term, df = count, then <count> postings:
<chunk> <tf> <dl>\n              chunk id (< n), term frequency (> 0), chunk token count
```

Output lines:

```text
c <d_seq> <start> <end>          one per chunk of document d_seq, in order (end exclusive)
v <q_seq> ok <offset>            quote found at offset in the normalized evidence
v <q_seq> badid | empty | missing
h <r_seq> <rank> <chunk> <score> one per hit, rank 0-based, score desc then chunk asc
```

## Answers

1. Retrieve: unique query tokens, their postings, `rank` with `k` sized to fill
   `evidence_tokens` (at most 12). Each hit becomes evidence `[E<n>] <source> p.<page>`
   plus its text.
2. Prompt: a fixed system prompt, then earlier questions and answers (without evidence),
   then the evidence and the question. The model is told to cite as
   `[E3: "words copied verbatim from E3"]` and to say when the evidence is insufficient.
3. Stream: one chat completion call. Reasoning is shown only as a dim status; the answer
   prints block by block.
4. Verify: every `[E<n>]` and `[E<n>: "..."]` is parsed (curly quotes and `【】` are
   accepted). Quotes go to `verify`. Each citation gets a status: `verified`, `failed`,
   `unquoted` or `badid`. Heph never rewrites or invents citations.

`heph ask --json` prints
`{answer, citations: [{id, source, page, quote, status}], evidence: [{id, source, page, text}], usage}`.

## Zero remote code execution

- Nothing is downloaded at install or at run time: no engines, models, binaries, plugins
  or updates.
- Nothing is loaded from an armory: no plugins, no prompt files. The model has no tools: no
  shell, no file access, no web.
- The only subprocess is the packaged `heph-core`, resolved inside the installed package
  and run with a fixed argv, no shell, stdin and stdout only. It contains no network or
  exec code.
- Network peers are the active login's server (`POST /chat/completions`, `GET /models`);
  a Codex login uses `chatgpt.com` for answers and `auth.openai.com` to sign in. Heph
  opens no browser: it prints the sign-in URL.
- Materials are untrusted: symlinks and path escapes are refused, sizes are capped, Office
  archives are checked for bombs and traversal, XML is parsed with `defusedxml`.
