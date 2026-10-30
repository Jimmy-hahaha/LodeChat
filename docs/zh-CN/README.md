<div align="center">

# LodeChat 完整文档

轻量的终端 LLM 对话客户端：流式输出、思考内容展示、Markdown 实时渲染，对接任意 OpenAI 兼容 API。

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](../../LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)]()

[English](../en/README.md) · **简体中文**

</div>

## 目录

- [快速开始](#快速开始)
- [配置项](#配置项)
- [运行与使用](#运行与使用)
- [内置命令](#内置命令)
- [日志与渲染](#日志与渲染)
- [其他行为](#其他行为)
- [项目结构](#项目结构)

## 快速开始

```bash
mkdir build && cd build
cmake .. && cmake --build .
./config_tool    # 问答式生成配置（不联网）
./LodeChat       # 打印 Logo，等待 >>> 提示符
```

构建依赖 CMake ≥ 3.20、支持 C++20 的编译器、OpenSSL 与 Threads；可选 `fmt`（存在时自动启用）、`mold` 链接器（自动探测，也可用 `-DLLMCHAT_LINKER=` 指定，也可设为 `default` 关闭）与 GNU `readline`（Linux / macOS 上自动探测，缺失时回退为 `std::getline`；Windows 不需要）。构建产物：

| 可执行文件 | 用途 |
| --- | --- |
| `LodeChat` | 对话客户端 |
| `config_tool` | 配置工具，问答式填写 `base_url` / `api_key` / `current_model`，直接回车保持原值 |

## 配置项

按平台约定放在用户配置目录，也可以放在可执行文件旁边或当前工作目录：

| 平台 | 配置目录 |
| --- | --- |
| **Linux** | `~/.config/llm-chat`（或 `$XDG_CONFIG_HOME/llm-chat`） |
| **macOS** | `~/Library/Application Support/llm-chat` |
| **Windows** | `%APPDATA%\llm-chat`（即 `C:\Users\用户名\AppData\Roaming\llm-chat`） |

目录下的 `config.json` 全部可用字段：

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

| 字段 | 默认值 | 说明 |
| --- | --- | --- |
| `base_url` | 空 | 后端服务地址，含协议与端口、不含 `/v1` 路径（误带 `/v1` 后缀会自动忽略）。首次生成配置文件时写入 `http://0.0.0.0:8088` 作为初值 |
| `models` | `[]` | 从端点发现的可用模型列表，`/models` 会写入这里，也可以手填。旧版配置里 `models` 是单个字符串，仍可被识别 |
| `current_model` | 空 | 当前对话使用的模型，由 `/models` 菜单选择后写入 |
| `api_key` | 空 | 调用后端服务所需的 API Key。为空时会交互式询问（输入隐藏） |
| `debug` / `log_level` | `true` / `info` | 预留项，当前版本尚未使用；写回配置时原样保留 |

**环境变量**（优先级高于配置文件）：

| 环境变量 | 覆盖 | 兼容写法 |
| --- | --- | --- |
| `API_URL` | `base_url` | `OPENAI_BASE_URL`、`OPENAI_API_URL` |
| `API_KEY` | `api_key` | `OPENAI_API_KEY` |

```bash
API_URL=http://127.0.0.1:8088 API_KEY=<key> ./LodeChat
```

**配置文件查找顺序**：`MYAPP_CONFIG` 环境变量指定的路径 → 用户配置目录 → 可执行文件所在目录 → 当前工作目录。四处都没有时会就近创建一份默认配置，接着在启动流程里问你要 `base_url` 与 `api_key`。

## 运行与使用

```bash
./LodeChat
```

目前只支持 **OpenAI 兼容 API** 后端服务（`/v1/chat/completions` 与 `/v1/models`）。

| 阶段 | 行为 |
| --- | --- |
| 启动 | 打印 ASCII Logo；`base_url` / `api_key` 缺失时交互式询问（`api_key` 输入隐藏）并自动保存 |
| 选模型 | 配置里没有可用模型时，启动即请求端点 `/v1/models` 并弹出菜单；只有一个候选则直接选中，不弹菜单；列表为空时提示手输模型名 |
| 输入 | 回车换行，**空行提交**，便于粘贴长文本；提交过的输入会进历史记录 |
| 会话 | `role` / `content` 组成的消息历史保留在内存中，多轮上下文连续；退出即清空 |
| 等待 | 请求期间显示旋转动画，收到第一个片段即停 |

每次请求的报文：

| 参数 | 值 |
| --- | --- |
| `model` | `current_model` |
| `stream` | `true` |
| `temperature` | `0.7` |
| `enable_thinking` | 由 `/think` 与 `/think_off` 控制 |
| `messages` | 完整会话历史 |

超时设置为连接 50 秒、读取 600 秒；连接失败或非 200 响应只写日志并在终端提示，不会中断会话。

请求前缀**只追加、不改写**：assistant 消息按服务端返回原样保存（含 `reasoning_content`），请求失败也不会写入空消息。这样每轮发出去的提示词就是上一轮的提示词加上一轮回复，逐字节一致，服务端的前缀缓存才有机会命中。llama.cpp 需要用 `--chat-template-kwargs '{"preserve_thinking": true}'` 启动，历史里的思考内容才会被渲染回 prompt。

## 内置命令

以 `/` 开头的输入不会被当作提问发出去，也不会进入待发送缓冲区：

| 命令 | 说明 |
| --- | --- |
| `/models` | 从端点重新拉取模型列表并写入配置，随后切换到全屏菜单：`↑`/`↓` 移动、`Home`/`End` 跳首尾、`Enter` 确认、`q`/`Esc`/`Ctrl+C` 取消；选完自动回到对话界面，选中的模型写入 `config.json`。拉取失败时退回显示缓存列表 |
| `/model` | 显示当前端点与模型（`base_url` 末尾的 `/` 与 `/v1` 会被裁掉再显示） |
| `/think` | 开启思考模式（`enable_thinking: true`） |
| `/think_off` | 关闭思考模式 |
| `/help` | 列出全部命令（列表与 Tab 补全共用同一张命令表） |
| `/exit` | 退出程序；`Ctrl+D`（Windows 为 `Ctrl+Z`）同样可以 |

行编辑能力按平台分两套：Linux / macOS 上探测到 GNU readline 就用 readline（上下键历史、Tab 补全命令），否则回退为 `std::getline`；Windows 用 `_getch` 实现的内置编辑器，支持上下键历史与左右键、`Home`/`End`/`Delete` 光标操作。

## 日志与渲染

**日志**默认写在可执行文件所在目录（和 `LodeChat`、`config_tool` 放在一起，通常是 `build/openai.log`），同时打印到终端；用 `LODE_LOG_FILE` 可以指定其他路径。启动时的工作目录不再影响日志位置：

```text
[2026-10-01 14:18:07.512] [error] GET /v1/models failed: cannot connect to http://127.0.0.1:8088
```

设 `LODE_LOG_CONSOLE=0`（或 `off`）可关掉终端输出、只写文件。日志级别 `warn` 及以上立即落盘。

**Markdown 渲染**由内置渲染器完成，覆盖标题、列表（含嵌套）、代码块、表格、引用块、分隔线等，无需额外依赖。

**思考内容**走单独通道：`reasoning_content` 以灰色 `[Thinking]` 前缀先流出，正文再交给渲染器，两者互不打扰。

**单行临时层**：在真终端上，进来的字节先按原样打到屏幕上（零延迟、打字机感），等这一行确定下来，再把屏幕上这一行擦掉、原地重画成渲染版。纯文本行两边一致、什么都不做，所以不闪；只有真含标记（代码块、表格等）的行才会重绘。非 TTY（重定向到文件、测试）时完全走直接渲染。

## 其他行为

| 项目 | 说明 |
| --- | --- |
| 配置写回 | 只覆盖 `base_url` / `models` / `current_model` / `api_key` 四个键，文件里已有的其他键（`debug` / `log_level` 等）保持不动；原文件读不出来时会重写一份 |
| 版本控制 | `config/config_example.json` 是纳入版本控制的配置模板；实际使用的 `config.json` 由 `.gitignore` 排除 |
| 端点探测 | `/v1/models` 只在需要时请求；响应里 `data[].id` 与 `models[].name` 两种结构都能解析，相邻的重复项会被去掉 |
| 缺配置 | `api_key` 始终为空时启动即报错退出（退出码 1）；没有可用模型时同样报错退出 |
| 失败处理 | 拉取模型失败、请求失败都只告警，不影响其他功能；`/models` 会退回缓存列表 |

## 项目结构

```text
├── CMakeLists.txt
├── config/config_example.json     # 配置模板（实际配置不纳入版本控制）
├── docs/{en,zh-CN}/README.md       # 完整文档
├── include/{cpp-httplib,json,spdlog}
└── src/                            # chat config config_tool input lineedit log main md models ui
```

## 开源协议

[MIT License](../../LICENSE)
