<div align="center">

# LodeChat — Full Documentation

A lightweight terminal LLM chat client: streaming output, thinking content and live Markdown rendering, talking to any OpenAI-compatible API.

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](../../LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)]()

**English** · [简体中文](../zh-CN/README.md)

</div>

## Contents

- [Quick start](#quick-start)
- [Configuration](#configuration)
- [Running and usage](#running-and-usage)
- [Built-in commands](#built-in-commands)
- [Logging and rendering](#logging-and-rendering)
- [Other behaviour](#other-behaviour)
- [Project structure](#project-structure)

## Quick start

```bash
mkdir build && cd build
cmake .. && cmake --build .
./config_tool    # generate the config interactively (no network access)
./LodeChat       # prints the logo and waits for the >>> prompt
```

Requires CMake ≥ 3.20, a C++20 compiler, OpenSSL and Threads; optional `fmt` (enabled when present), the `mold` linker (auto-detected, or set `-DLLMCHAT_LINKER=`, including `default` to opt out) and GNU `readline` (auto-detected on Linux / macOS, falling back to `std::getline`; not needed on Windows). Two executables are produced:

| Executable | Purpose |
| --- | --- |
| `LodeChat` | the chat client |
| `config_tool` | a wizard for `base_url` / `api_key` / `current_model`; press Enter to keep the current value |

## Configuration

The config lives in the platform's user config directory, but the executable's directory and the current working directory are also accepted:

| Platform | Configuration directory |
| --- | --- |
| **Linux** | `~/.config/llm-chat` (or `$XDG_CONFIG_HOME/llm-chat`) |
| **macOS** | `~/Library/Application Support/llm-chat` |
| **Windows** | `%APPDATA%\llm-chat` (i.e. `C:\Users\Username\AppData\Roaming\llm-chat`) |

All available fields in `config.json`:

```json
{
    "base_url": "http://127.0.0.1:8088",
    "models": ["qwen3-0.8b", "qwen3-4b"],
    "current_model": "qwen3-0.8b",
    "api_key": "",
    "debug": true,
    "log_level": "info"
}
```

| Field | Default | Description |
| --- | --- | --- |
| `base_url` | empty | Backend service address, including scheme and port but no `/v1` path (a trailing `/v1` is ignored). A freshly generated config is seeded with `http://0.0.0.0:8088` |
| `models` | `[]` | Models discovered from the endpoint; `/models` writes them here and you may also fill the list in by hand. Older configs where `models` is a single string are still recognized |
| `current_model` | empty | The model used for the conversation, written after picking from the `/models` menu |
| `api_key` | empty | API key required to call the backend service. When empty you are prompted for it (input hidden) |
| `debug` / `log_level` | `true` / `info` | Reserved options, not used in the current version; preserved as-is when the config is written back |

**Environment variables** (take precedence over the config file):

| Variable | Overrides | Also accepted |
| --- | --- | --- |
| `API_URL` | `base_url` | `OPENAI_BASE_URL`, `OPENAI_API_URL` |
| `API_KEY` | `api_key` | `OPENAI_API_KEY` |

```bash
API_URL=http://127.0.0.1:8088 API_KEY=<key> ./LodeChat
```

**Config file lookup order**: the path given by the `MYAPP_CONFIG` environment variable → the user config directory → the executable's directory → the current working directory. If none of them has a file, a default config is created there and the startup flow then asks you for `base_url` and `api_key`.

## Running and usage

```bash
./LodeChat
```

Only **OpenAI-compatible API** backends are supported (`/v1/chat/completions` and `/v1/models`).

| Stage | Behaviour |
| --- | --- |
| Startup | Prints the ASCII logo; missing `base_url` / `api_key` are asked for interactively (`api_key` input is hidden) and saved automatically |
| Model picking | When the config has no usable model, startup fetches `/v1/models` and shows the menu; a single candidate is selected silently, and an empty list falls back to entering a name by hand |
| Input | Enter inserts a newline, an **empty line submits**, handy for pasting long text; submitted lines go into the history |
| Conversation | The `role` / `content` message history is kept in memory, so multi-turn context is continuous; it is cleared on exit |
| Waiting | A spinner is shown while waiting, and stops as soon as the first chunk arrives |

Request payload:

| Parameter | Value |
| --- | --- |
| `model` | `current_model` |
| `stream` | `true` |
| `temperature` | `0.7` |
| `enable_thinking` | toggled by `/think` and `/think_off` |
| `messages` | the whole conversation history |

Timeouts are 50 s for connecting and 600 s for reading; a failed connection or a non-200 response is only logged and reported on the terminal — the session continues.

## Built-in commands

Input starting with `/` is never sent as a question and never enters the pending buffer:

| Command | Description |
| --- | --- |
| `/models` | Re-fetches the model list and saves it to the config, then opens a full-screen menu: `↑`/`↓` to move, `Home`/`End` to jump, `Enter` to confirm, `q`/`Esc`/`Ctrl+C` to cancel. The choice is written to `config.json` and the chat view is restored. On a failed fetch the cached list is shown |
| `/model` | Shows the current endpoint and model (a trailing `/` or `/v1` in `base_url` is trimmed first) |
| `/think` | Enables thinking mode (`enable_thinking: true`) |
| `/think_off` | Disables thinking mode |
| `/help` | Lists every command (the same table powers Tab completion) |
| `/exit` | Quits; `Ctrl+D` (`Ctrl+Z` on Windows) works too |

Line editing has two flavours: on Linux / macOS, GNU readline is used when detected (arrow-key history, Tab completion) and `std::getline` otherwise; on Windows a built-in editor based on `_getch` supports arrow-key history plus left/right, `Home`, `End` and `Delete`.

## Logging and rendering

**Logging** writes `openai.log` in the current directory and echoes to the terminal:

```text
[2026-10-01 14:18:07.512] [error] GET /v1/models failed: cannot connect to http://127.0.0.1:8088
```

Set `LODE_LOG_CONSOLE=0` (or `off`) to write the file only. Messages at `warn` and above are flushed immediately.

**Markdown rendering** is handled by the built-in renderer, covering headings, lists (including nesting), code blocks, tables, block quotes and rules, with no extra dependency.

**Thinking content** takes a separate path: `reasoning_content` streams out first behind a gray `[Thinking]` prefix, and the body then goes to the renderer, so the two never interfere.

**Single-line provisional layer**: on a real terminal, incoming bytes are printed verbatim first (zero latency, typewriter feel); once the line is settled it is erased and redrawn in place as the rendered version. Plain text lines are identical either way, so nothing happens and nothing flickers — only lines that really contain markup (code blocks, tables, …) are redrawn. On a non-TTY (redirected output, tests) plain rendering is used throughout.

## Other behaviour

| Item | Details |
| --- | --- |
| Writing back | Only `base_url` / `models` / `current_model` / `api_key` are overwritten; other keys already in the file (`debug` / `log_level`, …) are left untouched. An unreadable file is rewritten from scratch |
| Version control | `config/config_example.json` is the tracked template; the real `config.json` is excluded by `.gitignore` |
| Endpoint probing | `/v1/models` is requested only when needed; both `data[].id` and `models[].name` response shapes are parsed, and adjacent duplicates are removed |
| Missing config | An `api_key` that stays empty aborts startup (exit code 1), as does an unrecoverable lack of models |
| Failure handling | A failed model fetch or request is a warning only; `/models` falls back to the cached list |

## Project structure

```text
├── CMakeLists.txt
├── config/config_example.json     # config template (the real config is not versioned)
├── docs/{en,zh-CN}/README.md       # full documentation
├── include/{cpp-httplib,json,spdlog}
└── src/                            # chat config config_tool input lineedit log main md models ui
```

## Open source license

[MIT License](../../LICENSE)
