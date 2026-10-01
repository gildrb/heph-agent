# Configuration

## Models and logins

Pick and add models inside `heph`. Commands are forgiving: `/mod`, `/mdl` and `/modle`
all run `/model`.

| Command | Does |
| --- | --- |
| `/model` | picks a model from every login (the active one first), or "add a local server", or "log in"; logins that fail show why |
| `/model <name>` | switches login or model; any fragment works (`/model 18020`, `/model qwen`, `/model openrouter/qwen3-32b`) |
| `/model <url>` | adds a local or self-hosted server and switches to it; asks for a key if the server wants one |
| `/login` | picks a provider |
| `/login <provider>` | `openai`, `openrouter`, `deepseek`, `zai`: asks for an API key; `codex`: logs in to your ChatGPT subscription; `local <url>`: same as `/model <url>` |
| `/logout [login]` | removes a login and the key Heph saved for it; picks one when not named |

With no login, Heph uses a server on this machine at `http://127.0.0.1:8080/v1` (llama.cpp
`llama-server`) and its first model. Common local servers:

| Server | URL |
| --- | --- |
| llama.cpp `llama-server` | `http://127.0.0.1:8080/v1` |
| vLLM | `http://127.0.0.1:8000/v1` |
| SGLang | `http://127.0.0.1:30000/v1` |
| Ollama | `http://127.0.0.1:11434/v1` |

### Codex (ChatGPT subscription)

`/login codex` prints a sign-in URL. Open it in any browser and sign in. Then either:
- the browser is on the same machine: Heph catches the redirect on `localhost:1455`; press
  Enter;
- the browser is elsewhere (SSH, a server): copy the address the browser ends on (it starts
  with `http://localhost:1455/auth/callback?code=`) and paste it into Heph.

Heph never opens a browser or starts a program itself. Tokens refresh on their own; if a
refresh fails, run `/login codex` again.

### Where logins live

| File | Holds | Written by |
| --- | --- | --- |
| `~/.config/heph/logins.toml` | logins (provider, URL, key file), the active login and model, and the reasoning level | `/model`, `/login`, `/logout`, Shift+Tab |
| `~/.config/heph/keys/<login>` | an API key you pasted, or Codex tokens | `/model <url>`, `/login`; mode 0600, folder 0700 |

`$XDG_CONFIG_HOME/heph/` replaces `~/.config/heph/` when set. A login's key comes from, in
order: its `key_file` (a file you keep elsewhere), the key Heph saved in `keys/`, then the
provider's variable (`OPENAI_API_KEY`, `OPENROUTER_API_KEY`, `DEEPSEEK_API_KEY`,
`ZAI_API_KEY`). A `key_file` line is the way to use a key you already store, for example:

```toml
[logins."127.0.0.1:18020"]
provider = "local"
base_url = "http://127.0.0.1:18020/v1"
key_file = "/path/to/api-key"
```

### Reasoning levels

Shift+Tab moves to the next level of the model in use; the status bar shows it after the
model name, and each answer's stats line shows the level it used. `logins.toml` keeps it as
`reasoning = "medium"` and every model gets the nearest level it has (an effort with no
equal falls to the next one down, else the lowest). Without one, each model's default.

| Provider | Where the levels come from | Sent as |
| --- | --- | --- |
| OpenRouter | its `/models` list (`reasoning.supported_efforts`, `mandatory`, `default_effort`) | `reasoning: {effort}`; off: `{enabled: false}` |
| OpenAI API | Heph's table of OpenAI's model pages: o-series and gpt-5 low to high, gpt-5.1+ add off (`none`), 5.2+ xhigh, 5.6 and GPT-6 max | `reasoning_effort`; reasoning models get `max_completion_tokens` and no temperature |
| ChatGPT (Codex) | Heph's table of openai/codex `models.json`: low to max or ultra, per model | `reasoning: {effort, summary: "auto"}` |
| DeepSeek | V4 and flash: off, low, high, max | `thinking: {type}` + `reasoning_effort` |
| Z.AI | GLM-5.3: low, high, max; 5.2: off, high, max; 4.5 to 5.1: off, on | `thinking: {type}` (+ `reasoning_effort`) |
| Local server | by model name: Qwen 3.8+ off, low, medium, xhigh; other Qwen3 off, on; gpt-oss low to high; others none | Qwen: `chat_template_kwargs` (`enable_thinking`, `reasoning_effort`); gpt-oss: `reasoning_effort` |

A local server cannot report what its model takes and some reject unknown fields, so Heph
sends reasoning fields only to the model families above.

## Settings

`~/.config/heph/config.toml` is optional and only holds these:

```toml
max_tokens = 8192
temperature = 0.2
evidence_tokens = 3000  # evidence budget per question
```

| Key | Default | Meaning |
| --- | --- | --- |
| `max_tokens` | `8192` | completion token limit, reasoning included; a reasoning model that uses it all before answering is reported as an error |
| `temperature` | `0.2` | sampling temperature (Codex ignores it) |
| `evidence_tokens` | `3000` | how much evidence goes into a question (at most 12 passages) |

Unknown keys and wrong types are errors, reported with the file path. `heph config` shows
the settings and logins in use.

## Environment

For scripts and CI. `HEPH_BASE_URL` wins over the saved login.

| Variable | Does |
| --- | --- |
| `HEPH_BASE_URL` | use this OpenAI-compatible server instead of the saved login |
| `HEPH_API_KEY` | its key |
| `HEPH_MODEL` | the model to use |
| `HEPH_ARMORY_HOME` | where named armories live (default `~/.armories`) |

Heph makes one model call per question and keeps the start of the prompt the same across
turns (fixed system prompt, then history, then the evidence and the question), so servers
that reuse the KV cache answer follow-ups faster.
