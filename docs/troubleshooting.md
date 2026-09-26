# Troubleshooting

Heph reports errors with the cause and the fix; this page adds context.

## No model server

`No model server at http://127.0.0.1:8080/v1` means nothing answered at `base_url`.
Start one (`llama-server -m model.gguf --port 8080`) or point Heph at yours:

```bash
export HEPH_BASE_URL=http://127.0.0.1:11434/v1
heph config
```

## No model listed

With `model` empty, Heph uses the first id from `GET {base_url}/models`. If the server
lists none, load a model in the server or set `model` / `HEPH_MODEL`.

## 401 or 403 from the server

The provider needs a key. Set `HEPH_API_KEY`, or set `api_key_env` in `config.toml` to the
name of the variable that holds it. Heph stops with an error when `api_key_env` names an
unset variable.

## A file is not indexed

`heph index` lists every material it skipped and why: unsupported legacy format, binary
file, not UTF-8, over the size limit, unreadable PDF, unsafe Office archive, or a symlink.
Also check `.harnessignore` and hidden names (see [Armories](armories.md)).

## Scanned PDFs

PDFs made of images have no text layer. Run OCR (for example `ocrmypdf`) and add the
result instead.

## Citations are red or yellow

- Red `failed`: the quoted words are not in that evidence passage (after whitespace
  normalization). The model paraphrased or misquoted; treat that sentence with care.
- Red `badid`: the answer cites an evidence id that was never given.
- Yellow `unquoted`: the citation names a passage but quotes nothing, so it can't be checked.

Smaller models misquote more often. A lower `temperature` and a model that follows
instructions well help.

## The answer says the evidence is insufficient

Retrieval is lexical. Use the words your materials use, check `/sources` for what was
retrieved, or raise `evidence_tokens`.

## Slow answers

The stats line shows tok/s. Heph makes one call per question and keeps the prompt prefix
stable, so enable prompt caching in your server (llama-server does by default). Lower
`evidence_tokens` or `max_tokens` for shorter prompts and answers.

## `heph-core` is missing or fails

The binary ships inside the wheel at `heph/_bin/heph-core`. Reinstall Heph. From source,
run `core/build.sh` and `uv sync` again (see [CONTRIBUTING](../CONTRIBUTING.md)).
