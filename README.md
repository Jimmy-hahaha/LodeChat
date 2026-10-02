<div align="center">

# LodeChat

**留在终端里，把话聊完。**

对接任意 OpenAI 兼容 API，流式输出边生成边渲染，思考内容与正文分开显示。

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)]()
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey.svg)]()

[English](./docs/en/README.md) · [简体中文](./docs/zh-CN/README.md)

</div>

## 工作方式

```mermaid
flowchart LR
    T[终端] --> I[多行输入 / 空行提交]
    I --> C[LodeChat]
    C -->|POST /v1/chat/completions| A[OpenAI 兼容 API]
    A -->|SSE 增量| R[Markdown 渲染器]
    R --> T
```

| 输入 | 处理方式 |
| --- | --- |
| 普通行 | 追加进待发送的多行文本，回车换行，方便粘贴长文本 |
| 空行 | 把攒好的那段文本发出去 |
| 以 `/` 开头的行 | 本地命令，直接执行，不会进对话历史 |

三个要点：

- **只要一个 OpenAI 兼容端点** —— `base_url` / `api_key` 指向哪里就聊哪里，本地 llama.cpp 与云端 API 都行
- **边生成边渲染** —— 正文交给内置 Markdown 渲染器（标题、列表、代码块、表格、引用），思考内容以灰色前缀单独走
- **模型不用手写** —— `/models` 拉取端点的 `/v1/models` 列表，方向键选完直接写进配置

## 快速开始

### 1. 编译

```bash
mkdir build && cd build
cmake .. && cmake --build .
```

需要 CMake ≥ 3.20、支持 C++20 的编译器、OpenSSL、Threads；可选 `fmt`（存在时自动启用）、`mold` 链接器（自动探测，也可用 `-DLLMCHAT_LINKER=` 指定）与 GNU `readline`（Linux / macOS 自动探测，缺失时回退为普通行输入）。构建产物是 `LodeChat` 与 `config_tool` 两个可执行文件。

### 2. 配置

首次启动会交互式问一遍 `base_url` 与 `api_key`（后者输入隐藏）并自动落盘，也可以直接跑 `./config_tool` 问答式生成，或手写：

```json
{
    "base_url": "http://127.0.0.1:8088",
    "models": ["qwen3-0.8b", "qwen3-4b"],
    "current_model": "qwen3-0.8b",
    "api_key": ""
}
```

`api_key` 也能用环境变量给（优先级高于配置文件）：

```bash
API_URL=http://127.0.0.1:8088 API_KEY=<key> ./LodeChat
```

完整字段与各平台的配置目录见[配置项](./docs/zh-CN/README.md#配置项)。

### 3. 运行

```bash
./LodeChat    # 打印 Logo，等待 >>> 提示符
```

配置里还没有模型列表时，启动会先请求端点的 `/v1/models`，用菜单让你挑一个（只有一个候选就直接选中，不弹菜单）。

### 4. 使用

输入行回车换行、**空行提交**；回复流式显示，思考内容走灰色 `[Thinking]` 前缀，正文实时渲染 Markdown。内置命令：

| 命令 | 作用 |
| --- | --- |
| `/models` | 重新拉取模型列表，全屏菜单里用 `↑`/`↓` 选、`Enter` 确认、`q`/`Esc` 取消 |
| `/model` | 显示当前端点与模型 |
| `/think` / `/think_off` | 开 / 关思考模式（`enable_thinking`） |
| `/help` | 列出全部命令 |
| `/exit` | 退出（`Ctrl+D` 同样可以） |

## 回复长什么样

思考内容与正文分两条路：先以灰色前缀逐字流出，正文再交给渲染器。屏幕上大致是这样：

````text
>>> 帮我写个二分查找

[Thinking]
用户要的是二分查找实现……先确认边界处理。

下面是一个标准实现：

```cpp
int lower_bound(const std::vector<int>& a, int target) {
    int lo = 0, hi = static_cast<int>(a.size());
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}
```
````

含标记的行会先按原样打出去再原地重画成渲染版，所以纯文本行不会闪；只有代码块、表格这类真需要排版的行才会重绘。

## 文档

| 文档 | 内容 |
| --- | --- |
| [完整文档（简体中文）](./docs/zh-CN/README.md) | 全部配置项、内置命令、日志与渲染行为 |
| [Full documentation (English)](./docs/en/README.md) | Everything above, in English |

## 开源协议

[MIT License](LICENSE)
