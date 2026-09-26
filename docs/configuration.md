# Configuration

Heph reads one optional file, `~/.config/heph/config.toml` (`$XDG_CONFIG_HOME/heph/config.toml`
when `XDG_CONFIG_HOME` is set). Without it, Heph uses a local server at
`http://127.0.0.1:8080/v1` and its first model. `heph config` shows the settings in use.

```toml
base_url = "http://127.0.0.1:8080/v1"
model = ""              # empty: first id from GET {base_url}/models
api_key_env = ""        # name of an env var holding a key, for hosted providers
api_key_file = ""       # or: absolute path of a file holding the key (not both)
max_tokens = 8192
temperature = 0.2
evidence_tokens = 3000  # evidence budget per question
```

| Key | Default | Meaning |
| --- | --- | --- |
| `base_url` | `http://127.0.0.1:8080/v1` | OpenAI-compatible API root (`http://` or `https://`) |
| `model` | empty | model id; empty picks the first id the server lists |
| `api_key_env` | empty | env var that holds the API key; must be set when named |
| `api_key_file` | empty | absolute path of a file that holds the API key (`~` allowed); must be readable and non-empty; exclusive with `api_key_env` |
| `max_tokens` | `8192` | completion token limit, reasoning included; a reasoning model that uses it all before answering is reported as an error |
| `temperature` | `0.2` | sampling temperature |
| `evidence_tokens` | `3000` | how much evidence goes into a question (at most 12 passages) |

Unknown keys and wrong types are errors, reported with the file path.

## Environment

| Variable | Overrides |
| --- | --- |
| `HEPH_BASE_URL` | `base_url` |
| `HEPH_MODEL` | `model` |
| `HEPH_API_KEY` | the API key (takes precedence over `api_key_env` and `api_key_file`) |
| `HEPH_ARMORY_HOME` | where named armories live (default `~/.armories`) |

## Servers

Local servers usually need no key. A server started with an API key (for example
`vllm serve --api-key` or `sglang --api-key`) can read it from a file:

```toml
base_url = "http://127.0.0.1:18020/v1"
api_key_file = "~/models/api-key"
```

Common local servers:

```toml
base_url = "http://127.0.0.1:8080/v1"   # llama.cpp llama-server
# base_url = "http://127.0.0.1:8000/v1" # vLLM
# base_url = "http://127.0.0.1:30000/v1" # SGLang
# base_url = "http://127.0.0.1:11434/v1" # Ollama
```

A hosted provider needs a key. Keep the key in the environment, not in the file:

```toml
base_url = "https://openrouter.ai/api/v1"
model = "qwen/qwen3-32b"
api_key_env = "OPENROUTER_API_KEY"
```

Heph makes one chat completion call per question and keeps the start of the prompt the
same across turns (fixed system prompt, then history, then the evidence and the question),
so servers that reuse the KV cache answer follow-ups faster.
