# Models and logins

## Which model does Heph use?

The model and login in use are shown at the bottom of the screen. By default Heph talks
to a local OpenAI-compatible server at http://127.0.0.1:8080/v1, such as llama.cpp,
LM Studio, Ollama or vLLM, and uses the first model that server lists.

## How do I change the model?

Type `/model`. It lists every model of every login; pick one with the arrow keys and
Enter, or type to filter. `/model qwen` switches straight to the closest match.
Your choice is saved and used next time.

## How do I use my own local server?

Type `/model` and pick "add a local server", then type its URL, for example
http://127.0.0.1:1234/v1. You can also type `/model http://127.0.0.1:1234/v1`.
If the server wants an API key, Heph asks for it once and saves it.

## How do I log in to OpenAI, OpenRouter, DeepSeek or Z.AI?

Type `/login` and pick the provider, then paste your API key. Press Enter without a key
to use the key in the provider's environment variable instead, like OPENAI_API_KEY.
Then pick a model with `/model`. `/logout` removes a login and its saved key.

## How do I use my ChatGPT subscription?

Type `/login` and pick "ChatGPT subscription (Codex)". Heph prints a sign-in URL; open it
in any browser. When the browser shows success, press Enter. If the browser runs on
another machine, paste the address it lands on (http://localhost:1455/auth/callback?code=...).

## Where are logins and keys kept?

Logins are listed in `~/.config/heph/logins.toml`. Each key is a private file in
`~/.config/heph/keys/`, readable only by you. Answer settings (max_tokens, temperature,
evidence_tokens) are in `~/.config/heph/config.toml`; `heph config` shows them all.
For scripts, HEPH_BASE_URL, HEPH_API_KEY and HEPH_MODEL override the saved login.
