#pragma once
// ───────── 行输入：readline / 内置编辑器二选一，另提供 raw 模式单键读取 ─────────

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <conio.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#endif

#if defined(LODECHAT_HAVE_READLINE)
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace line {

// ───────── 命令表：readline 补全与 /help 共用，避免两处维护 ─────────
inline const std::vector<std::pair<std::string, std::string>>& commands() {
    static const std::vector<std::pair<std::string, std::string>> table = {
        {"/help",      "show this help"},
        {"/models",    "fetch models from the endpoint and pick one"},
        {"/model",     "show the current endpoint and model"},
        {"/think",     "enable thinking mode"},
        {"/think_off", "disable thinking mode"},
        {"/exit",      "exit"},
    };
    return table;
}

// ───────── 终端信息：查终端行列数，优先 ioctl，退回 COLUMNS / LINES，缺省 80x24 ─────────

inline int terminal_columns() {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        int cols = info.srWindow.Right - info.srWindow.Left + 1;
        if (cols > 0) return cols;
    }
#else
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
#endif
    if (const char* env = std::getenv("COLUMNS")) {
        int cols = std::atoi(env);
        if (cols > 0) return cols;
    }
    return 80;
}

inline int terminal_rows() {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
        int rows = info.srWindow.Bottom - info.srWindow.Top + 1;
        if (rows > 0) return rows;
    }
#else
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return ws.ws_row;
#endif
    if (const char* env = std::getenv("LINES")) {
        int rows = std::atoi(env);
        if (rows > 0) return rows;
    }
    return 24;
}

inline void enable_virtual_terminal() {
#if defined(_WIN32)
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

// ───────── 按键读取：raw 模式下把按键与转义序列归一成 KeyEvent ─────────

enum class Key {
    None, Enter, Up, Down, Left, Right, Home, End,
    Backspace, Delete, Escape, Interrupt, Character
};

struct KeyEvent {
    Key key = Key::None;
    char ch = 0;
};

class RawMode {
public:
    RawMode() { enter(); }
    ~RawMode() { leave(); }
    RawMode(const RawMode&) = delete;
    RawMode& operator=(const RawMode&) = delete;

private:
    void enter() {
#if defined(_WIN32)
        handle_ = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        if (handle_ == INVALID_HANDLE_VALUE || !GetConsoleMode(handle_, &mode)) return;
        saved_mode_ = mode;
        SetConsoleMode(handle_, (mode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                                          ENABLE_PROCESSED_INPUT)) | ENABLE_EXTENDED_FLAGS);
        active_ = true;
#else
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) return;
        termios t = saved_;
        t.c_lflag &= ~(ICANON | ECHO | ISIG);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0) active_ = true;
#endif
    }

    void leave() {
#if defined(_WIN32)
        if (active_) {
            SetConsoleMode(handle_, saved_mode_);
            active_ = false;
        }
#else
        if (active_) {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
            active_ = false;
        }
#endif
    }

#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DWORD saved_mode_ = 0;
#else
    termios saved_{};
#endif
    bool active_ = false;
};

#if !defined(_WIN32)
inline bool read_byte_with_timeout(char& out, int timeout_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0) return false;
    return ::read(STDIN_FILENO, &out, 1) == 1;
}
#endif

inline KeyEvent read_key() {
    KeyEvent ev;
#if defined(_WIN32)
    int c = _getch();
    if (c == 0 || c == 0xE0) {
        switch (_getch()) {
            case 72: ev.key = Key::Up; break;
            case 80: ev.key = Key::Down; break;
            case 75: ev.key = Key::Left; break;
            case 77: ev.key = Key::Right; break;
            case 71: ev.key = Key::Home; break;
            case 79: ev.key = Key::End; break;
            case 83: ev.key = Key::Delete; break;
            default: break;
        }
        return ev;
    }
    switch (c) {
        case '\r': case '\n': ev.key = Key::Enter; return ev;
        case 8: case 127: ev.key = Key::Backspace; return ev;
        case 27: ev.key = Key::Escape; return ev;
        case 3: ev.key = Key::Interrupt; return ev;
        default: break;
    }
    if (c < 32) return ev;
    ev.key = Key::Character;
    ev.ch = static_cast<char>(c);
    return ev;
#else
    unsigned char c = 0;
    if (::read(STDIN_FILENO, &c, 1) != 1) {
        ev.key = Key::Escape;
        return ev;
    }
    if (c == 27) {
        char next = 0;
        if (!read_byte_with_timeout(next, 50)) {
            ev.key = Key::Escape;
            return ev;
        }
        if (next != '[' && next != 'O') {
            ev.key = Key::Escape;
            return ev;
        }
        std::string seq;
        char ch = 0;
        while (read_byte_with_timeout(ch, 50)) {
            seq.push_back(ch);
            if (ch >= '@' && ch <= '~') break;
        }
        if (seq == "A") ev.key = Key::Up;
        else if (seq == "B") ev.key = Key::Down;
        else if (seq == "C") ev.key = Key::Right;
        else if (seq == "D") ev.key = Key::Left;
        else if (seq == "H" || seq == "1~" || seq == "7~") ev.key = Key::Home;
        else if (seq == "F" || seq == "4~" || seq == "8~") ev.key = Key::End;
        else if (seq == "3~") ev.key = Key::Delete;
        return ev;
    }
    switch (c) {
        case '\r': case '\n': ev.key = Key::Enter; return ev;
        case 8: case 127: ev.key = Key::Backspace; return ev;
        case 3: ev.key = Key::Interrupt; return ev;
        default: break;
    }
    if (c < 32) return ev;
    ev.key = Key::Character;
    ev.ch = static_cast<char>(c);
    return ev;
#endif
}

// ───────── 独占全屏：菜单等全屏 UI 在独立屏幕里绘制 ─────────
class AlternateScreen {
public:
    AlternateScreen() {
        std::cout << "\033[?1049h\033[2J\033[H" << std::flush;
    }
    ~AlternateScreen() {
        std::cout << "\033[?1049l" << std::flush;
    }
    AlternateScreen(const AlternateScreen&) = delete;
    AlternateScreen& operator=(const AlternateScreen&) = delete;

    static void clear_frame() {
        std::cout << "\033[H\033[J";
    }
};

// ───────── 历史记录：readline 用自带历史，其余平台用进程内列表去重追加 ─────────

inline std::vector<std::string>& local_history() {
    static std::vector<std::string> history;
    return history;
}

inline void add_history(const std::string& entry) {
    if (entry.empty()) return;
#if defined(LODECHAT_HAVE_READLINE) && !defined(_WIN32)
    ::add_history(entry.c_str());
#else
    std::vector<std::string>& history = local_history();
    if (history.empty() || history.back() != entry) history.push_back(entry);
#endif
}

// ───────── 行输入：readline / Windows 内置编辑器 / std::getline 三选一，返回一行或 EOF ─────────

#if defined(_WIN32)
inline std::optional<std::string> win_read_line(const std::string& prompt) {
    std::vector<std::string>& history = local_history();
    std::string buffer;
    size_t cursor = 0;
    size_t history_pos = history.size();
    std::string pending;

    auto redraw = [&] {
        std::cout << "\r\033[K" << prompt << buffer;
        size_t back = buffer.size() - cursor;
        if (back > 0) std::cout << "\033[" << back << "D";
        std::cout << std::flush;
    };

    std::cout << prompt << std::flush;
    while (true) {
        int c = _getch();
        if (c == 0 || c == 0xE0) {
            switch (_getch()) {
                case 72:
                    if (!history.empty() && history_pos > 0) {
                        if (history_pos == history.size()) pending = buffer;
                        buffer = history[--history_pos];
                        cursor = buffer.size();
                        redraw();
                    }
                    break;
                case 80:
                    if (history_pos < history.size()) {
                        buffer = (++history_pos == history.size()) ? pending : history[history_pos];
                        cursor = buffer.size();
                        redraw();
                    }
                    break;
                case 75:
                    if (cursor > 0) { --cursor; redraw(); }
                    break;
                case 77:
                    if (cursor < buffer.size()) { ++cursor; redraw(); }
                    break;
                case 71:
                    cursor = 0; redraw(); break;
                case 79:
                    cursor = buffer.size(); redraw(); break;
                case 83:
                    if (cursor < buffer.size()) {
                        buffer.erase(cursor, 1);
                        redraw();
                    }
                    break;
                default: break;
            }
            continue;
        }
        if (c == '\r' || c == '\n') {
            std::cout << '\n' << std::flush;
            return buffer;
        }
        if (c == 26) {
            std::cout << '\n' << std::flush;
            return std::nullopt;
        }
        if (c == 3) {
            buffer.clear();
            cursor = 0;
            history_pos = history.size();
            pending.clear();
            redraw();
            continue;
        }
        if (c == 8 || c == 127) {
            if (cursor > 0) {
                buffer.erase(cursor - 1, 1);
                --cursor;
                redraw();
            }
            continue;
        }
        if (c < 32) continue;
        buffer.insert(buffer.begin() + static_cast<std::ptrdiff_t>(cursor), static_cast<char>(c));
        ++cursor;
        redraw();
    }
}
#endif

#if defined(LODECHAT_HAVE_READLINE) && !defined(_WIN32)
inline char* command_generator(const char* text, int state) {
    static size_t index = 0;
    static size_t length = 0;
    if (state == 0) {
        index = 0;
        length = std::strlen(text);
    }
    const auto& table = commands();
    while (index < table.size()) {
        const std::string& name = table[index++].first;
        if (name.compare(0, length, text) == 0) {
            return ::strdup(name.c_str());
        }
    }
    return nullptr;
}

inline void init_readline() {
    static bool initialized = false;
    if (initialized) return;
    rl_initialize();
    rl_completion_entry_function = command_generator;
    rl_attempted_completion_function = nullptr;
    initialized = true;
}
#endif

inline std::optional<std::string> read_line(const std::string& prompt) {
#if defined(LODECHAT_HAVE_READLINE) && !defined(_WIN32)
    init_readline();
    char* raw = ::readline(prompt.c_str());
    if (!raw) {
        std::cout << '\n' << std::flush;
        return std::nullopt;
    }
    std::string result(raw);
    std::free(raw);
    return result;
#elif defined(_WIN32)
    if (_isatty(_fileno(stdin))) return win_read_line(prompt);
    std::cout << prompt << std::flush;
    std::string entry;
    if (!std::getline(std::cin, entry)) {
        std::cout << '\n' << std::flush;
        return std::nullopt;
    }
    if (!entry.empty() && entry.back() == '\r') entry.pop_back();
    return entry;
#else
    std::cout << prompt << std::flush;
    std::string entry;
    if (!std::getline(std::cin, entry)) {
        std::cout << '\n' << std::flush;
        return std::nullopt;
    }
    return entry;
#endif
}

}
