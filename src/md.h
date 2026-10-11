#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/ioctl.h>
#include <unistd.h>
#endif

// ───────── Markdown 渲染：把流式 chunk 增量渲染成终端排版 ─────────
namespace md {

// ───────── 标记与层级：容器 / 行内 / 受保护三层，决定标记能否跨行 ─────────
enum class Delim {
    Bold, Italic, BoldItalic, Code, MathInline,
    Fence, MathDisplay, Table,
    Quote, List,
};
enum class Layer { Container, Inline, Protected };
enum class Align { Left, Center, Right };
inline Layer layer_of(Delim d) {
    switch (d) {
        case Delim::Quote:
        case Delim::List:        return Layer::Container;
        case Delim::Fence:
        case Delim::MathDisplay:
        case Delim::Table:       return Layer::Protected;
        default:                 return Layer::Inline;
    }
}
// ───────── 容器帧：引用 / 列表 / 表格等跨行结构在解析途中的状态 ─────────
struct Frame {
    Delim       delim{};
    Layer       layer{Layer::Inline};
    std::string buf;
    std::string literal;
    std::string aux;
    int         indent = 0;
    bool        ordered = false;
    int         index = 1;
    std::vector<std::string>              headers;
    std::vector<std::vector<std::string>> rows;
    std::vector<Align>                    aligns;
    bool                                  header_done = false;
    bool                                  header_set  = false;
};
// ───────── 定界符匹配：一次命中的类型、长度与左右可开可闭 ─────────
struct DelimMatch {
    Delim       kind;
    size_t      len;
    std::string literal;
    bool        can_open;
    bool        can_close;
};
// ───────── 定界符扫描：按 UTF-8 码点判左右可开可闭，对齐 Markdown 的 flanking 规则 ─────────
class DelimScanner {
public:
    static std::pair<char32_t, size_t> decode(const std::string& s, size_t i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) return {c, 1};
        if ((c >> 5) == 0x6 && i + 1 < s.size())
            return {((c & 0x1Fu) << 6) | (s[i+1] & 0x3Fu), 2};
        if ((c >> 4) == 0xE && i + 2 < s.size())
            return {((c & 0x0Fu) << 12) | ((s[i+1] & 0x3Fu) << 6) | (s[i+2] & 0x3Fu), 3};
        if ((c >> 3) == 0x1E && i + 3 < s.size())
            return {((c & 0x07u) << 18) | ((s[i+1] & 0x3Fu) << 12)
                    | ((s[i+2] & 0x3Fu) << 6) | (s[i+3] & 0x3Fu), 4};
        return {0xFFFD, 1};
    }
    static bool cp_is_space(char32_t cp) {
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r'
            || cp == '\f' || cp == '\v') return true;
        if (cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200A)
            || cp == 0x2028 || cp == 0x2029 || cp == 0x3000) return true;
        return false;
    }
    static bool cp_is_punct(char32_t cp) {
        if (cp < 0x80) {
            return (cp >= 0x21 && cp <= 0x2F)
                || (cp >= 0x3A && cp <= 0x40)
                || (cp >= 0x5B && cp <= 0x60)
                || (cp >= 0x7B && cp <= 0x7E);
        }
        return (cp >= 0x2000 && cp <= 0x206F)
            || (cp >= 0x3000 && cp <= 0x303F)
            || (cp >= 0xFF00 && cp <= 0xFF0F)
            || (cp >= 0xFF1A && cp <= 0xFF20)
            || (cp >= 0xFF3B && cp <= 0xFF40)
            || (cp >= 0xFF5B && cp <= 0xFF65);
    }
    static bool is_space_at(const std::string& s, size_t i) {
        if (i >= s.size()) return true;
        auto [cp, n] = decode(s, i); (void)n;
        return cp_is_space(cp);
    }
    static bool is_punct_at(const std::string& s, size_t i) {
        if (i >= s.size()) return false;
        auto [cp, n] = decode(s, i); (void)n;
        return cp_is_punct(cp);
    }
    static std::pair<bool, bool> flanking(const std::string& s, size_t i, size_t n,
                                          bool has_prev = false, char32_t prev_cp = 0) {
        bool prev_space = (i > 0) ? is_space_at(s, i - 1)
                                  : (has_prev ? cp_is_space(prev_cp) : true);
        bool prev_punct = (i > 0) ? is_punct_at(s, i - 1)
                                  : (has_prev && cp_is_punct(prev_cp));
        bool next_space = (i + n >= s.size()) || is_space_at(s, i + n);
        bool next_punct = (i + n < s.size()) && is_punct_at(s, i + n);
        bool left = !next_space && (!next_punct || prev_space || prev_punct);
        bool right = !prev_space && (!prev_punct || next_space || next_punct);
        return {left, right};
    }
    static std::optional<DelimMatch> match(const std::string& s, size_t i, bool in_word,
                                           bool has_prev = false, char32_t prev_cp = 0) {
        if (i >= s.size()) return std::nullopt;
        if (s[i] == '*') {
            size_t n = 1;
            while (i + n < s.size() && s[i + n] == '*') ++n;
            if (n >= 3) n = 3;
            auto [left, right] = flanking(s, i, n, has_prev, prev_cp);
            Delim kind = (n == 3) ? Delim::BoldItalic
                       : (n == 2) ? Delim::Bold
                                  : Delim::Italic;
            return DelimMatch{kind, n, std::string(n, '*'), left, right};
        }
        if (s[i] == '_' && !in_word) {
            auto [left, right] = flanking(s, i, 1, has_prev, prev_cp);
            bool prev_bound = true;
            if (i > 0)      prev_bound = is_space_at(s, i - 1) || is_punct_at(s, i - 1);
            else if (has_prev) prev_bound = cp_is_space(prev_cp) || cp_is_punct(prev_cp);
            bool next_bound = (i + 1 >= s.size()) || is_space_at(s, i + 1) || is_punct_at(s, i + 1);
            return DelimMatch{Delim::Italic, 1, "_", left && prev_bound, right && next_bound};
        }
        if (s[i] == '`') return DelimMatch{Delim::Code, 1, "`", true, true};
        if (s[i] == '$') {
            if (i + 1 < s.size() && s[i + 1] == '$') {
                auto [left, right] = flanking(s, i, 2, has_prev, prev_cp);
                return DelimMatch{Delim::MathInline, 2, "$$", left, right};
            }
            auto [left, right] = flanking(s, i, 1, has_prev, prev_cp);
            return DelimMatch{Delim::MathInline, 1, "$", left, right};
        }
        return std::nullopt;
    }
};
// ───────── 渲染钩子：帧渲染与块级开闭可替换，供测试或非标准输出覆盖 ─────────
struct RenderHooks {
    std::function<std::string(const Frame&)> render;
    std::function<std::optional<std::string>(const std::string&)> try_block_leaf;
    std::function<std::optional<Frame>(const std::string&)> try_block_open;
};
RenderHooks default_hooks();

// ───────── 流式渲染器：feed_chunk 增量喂入，行一确定就排版输出 ─────────
class StreamRenderer {
public:
    StreamRenderer();
    explicit StreamRenderer(RenderHooks hooks) : hooks_(std::move(hooks)) {}
    void set_output(std::ostream* os) {
        out_ = os;
        if (os == &std::cout) tty_out_ = fd_is_tty(1);
    }
    void set_provisional(bool on) { provisional_ = on; }
    void set_tty(bool on)         { tty_out_ = on; }
    void set_terminal_size(int cols, int rows) { cols_ = cols; rows_ = rows; }
    bool provisional_active() const { return provisional_ && tty_out_; }
    const std::string& buffered_output() const { return out_default_; }
    void feed_chunk(const std::string& bytes) {
        sl_open_ = true;
        if (provisional_active())
            line_cols_ = cols_ > 0 ? cols_ : fd_terminal_cols(1);
        size_t i = 0;
        while (i < bytes.size()) {
            size_t nl = bytes.find('\n', i);
            if (nl == std::string::npos) {
                stream_chunk(bytes.substr(i));
                return;
            }
            size_t end = nl;
            if (end > i && bytes[end - 1] == '\r') --end;
            stream_chunk(bytes.substr(i, end - i));
            end_current_line();
            i = nl + 1;
        }
    }
    void push(const std::string& s) {
        if (s.empty()) return;
        if (!stack_.empty() && stack_.back().layer == Layer::Protected) {
            if (sl_open_ && sl_active()) {
                blk_committed_ = true;
                line_put(s);
            }
            stack_.back().buf += s;
            return;
        }
        scan_inline(s);
        if (out_) out_->flush();
    }
    void end_line() {
        if (!cur_.empty()) {
            end_current_line();
            return;
        }
        if (!stack_.empty() && stack_.back().layer == Layer::Protected) {
            if (sl_open_ && sl_active()) {
                blk_committed_ = true;
                line_put("\n");
            }
            stack_.back().buf += '\n';
            return;
        }
        flush_line();
    }
    void feed(const std::string& line) {
        has_prev_ = false;
        prev_cp_  = 0;
        if (!stack_.empty() && stack_.back().layer == Layer::Protected) {
            std::string content = strip_protected_prefix(line);
            if (stack_.back().delim == Delim::MathDisplay)
                handle_math_display(content, false);
            else
                handle_protected(content, line);
            return;
        }
        if (has_inline_frame()) {
            scan_inline(line);
            flush_line();
            return;
        }
        std::string content = strip_container(line);
        if (content.empty()) {
            force_close_inline();
            containers_.erase(
                std::remove_if(containers_.begin(), containers_.end(),
                    [](const Frame& f){ return f.delim == Delim::List; }),
                containers_.end());
            flush_line();
            return;
        }
        if (hooks_.try_block_leaf) {
            if (auto ansi = hooks_.try_block_leaf(content)) {
                emit(*ansi);
                flush_line();
                return;
            }
        }
        if (hooks_.try_block_open) {
            if (auto f = hooks_.try_block_open(content)) {
                Delim d = f->delim;
                stack_.push_back(std::move(*f));
                blk_rows_      = line_take();
                blk_committed_ = sl_active() && blk_rows_ > max_blk_rows();
                if (d == Delim::Table)
                    handle_protected(content);
                else if (d == Delim::MathDisplay)
                    handle_math_display(content, true);
                return;
            }
        }
        scan_inline(content);
        flush_line();
    }
    void finish() {
        if (para_streaming_ || !scan_pending_.empty() || !cur_.empty())
            end_current_line();
        force_close_inline();
        while (!stack_.empty()) {
            Frame f = std::move(stack_.back());
            stack_.pop_back();
            if (f.delim == Delim::Table) {
                size_t cur = line_take();
                size_t total = blk_rows_ + cur;
                if (sl_active() && total > max_blk_rows()) blk_committed_ = true;
                bool degraded = sl_active() && blk_committed_;
                if (!degraded && sl_active()) blk_erase(total);
                if (!degraded) {
                    if (!f.header_done) {
                        emit(plain_table(f));
                    } else {
                        emit_block(render_fitted_table(f));
                    }
                }
                blk_rows_      = 0;
                blk_committed_ = false;
            } else if (f.layer == Layer::Protected) {
                size_t cur = line_take();
                size_t total = blk_rows_ + cur;
                if (sl_active() && total > max_blk_rows()) blk_committed_ = true;
                bool degraded = sl_active() && blk_committed_;
                if (!degraded && sl_active()) blk_erase(total);
                if (!degraded) {
                    line_started_ = false;
                    emit_block(f.literal + "\n" + f.buf);
                }
                blk_rows_      = 0;
                blk_committed_ = false;
            } else if (f.layer == Layer::Inline) {
                emit(f.literal + f.buf);
            } else {
                emit(hooks_.render ? hooks_.render(f) : f.buf);
            }
        }
        if (line_started_ || !line_prefix_.empty()) flush_line();
    }
    static std::string trim(const std::string& s) {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
        while (b > a && (s[b-1] == ' ' || s[b-1] == '\t')) --b;
        return s.substr(a, b - a);
    }
    static bool is_table_row(const std::string& s) {
        bool has_pipe = false;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\') { ++i; continue; }
            if (s[i] == '|') { has_pipe = true; break; }
        }
        return has_pipe;
    }
    static std::vector<std::string> split_row(const std::string& s) {
        std::vector<std::string> cells;
        std::string cur;
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i < s.size() && s[i] == '|') ++i;
        for (; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size() && s[i+1] == '|') {
                cur += '|'; ++i;
            } else if (s[i] == '|') {
                cells.push_back(trim(cur));
                cur.clear();
            } else {
                cur += s[i];
            }
        }
        std::string last = trim(cur);
        if (!last.empty()) cells.push_back(last);
        return cells;
    }
    static bool is_separator_cell(const std::string& s) {
        size_t i = 0, n = s.size(), dashes = 0;
        if (i < n && s[i] == ':') ++i;
        while (i < n && s[i] == '-') { ++i; ++dashes; }
        if (dashes == 0) return false;
        if (i < n && s[i] == ':') ++i;
        return i == n;
    }
    static bool is_separator_row(const std::vector<std::string>& cells) {
        if (cells.empty()) return false;
        for (auto& c : cells) if (!is_separator_cell(c)) return false;
        return true;
    }
    static std::vector<Align> parse_aligns(const std::vector<std::string>& cells) {
        std::vector<Align> a;
        for (auto& c : cells) {
            bool l = !c.empty() && c.front() == ':';
            bool r = !c.empty() && c.back()  == ':';
            a.push_back(l && r ? Align::Center : (r ? Align::Right : Align::Left));
        }
        return a;
    }
    static bool is_wide_cp(char32_t cp) {
        return (cp >= 0x1100 && cp <= 0x115F)
            || (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F)
            || (cp >= 0xAC00 && cp <= 0xD7A3)
            || (cp >= 0xF900 && cp <= 0xFAFF)
            || (cp >= 0xFE30 && cp <= 0xFE6F)
            || (cp >= 0xFF00 && cp <= 0xFF60)
            || (cp >= 0xFFE0 && cp <= 0xFFE6)
            || (cp >= 0x1F300 && cp <= 0x1FAFF);
    }
    static size_t display_width(const std::string& s) {
        size_t w = 0, i = 0;
        while (i < s.size()) {
            auto [cp, n] = DelimScanner::decode(s, i);
            w += is_wide_cp(cp) ? 2 : 1;
            i += n;
        }
        return w;
    }
    static bool has_visible(const std::string& s) {
        size_t i = 0;
        while (i < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == 0x1b) {
                ++i;
                if (i < s.size() && s[i] == '[') {
                    ++i;
                    while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) ++i;
                    if (i < s.size()) ++i;
                } else if (i < s.size()) {
                    ++i;
                }
                continue;
            }
            if (c >= 0x20 && c != 0x7F) return true;
            ++i;
        }
        return false;
    }
    static std::vector<std::string> wrap_display(const std::string& s, size_t width) {
        if (width == 0) width = 1;
        std::vector<std::string> lines;
        std::string line;
        size_t used = 0, i = 0;
        while (i < s.size()) {
            auto [cp, n] = DelimScanner::decode(s, i);
            if (cp == '\n') {
                lines.push_back(line);
                line.clear();
                used = 0;
                i += n;
                continue;
            }
            size_t cw = is_wide_cp(cp) ? 2 : 1;
            if (!line.empty() && used + cw > width) {
                lines.push_back(line);
                line.clear();
                used = 0;
            }
            line.append(s, i, n);
            used += cw;
            i += n;
        }
        lines.push_back(line);
        return lines;
    }
    static std::string plain_table(const Frame& f) {
        auto join = [](const std::vector<std::string>& cells) {
            std::string row;
            for (size_t i = 0; i < cells.size(); ++i) {
                if (i) row += " | ";
                row += cells[i];
            }
            return row;
        };
        std::string out;
        if (!f.headers.empty()) out += join(f.headers) + "\n";
        for (auto& r : f.rows) out += join(r) + "\n";
        return out;
    }
    static std::string render_table(const Frame& f, size_t max_width = 0) {
        size_t cols = f.headers.size();
        for (auto& r : f.rows) cols = std::max(cols, r.size());
        if (cols == 0) return "";
        std::vector<size_t> w(cols, 3);
        auto upd = [&](const std::vector<std::string>& row) {
            for (size_t c = 0; c < row.size(); ++c)
                w[c] = std::max(w[c], display_width(row[c]));
        };
        upd(f.headers);
        for (auto& r : f.rows) upd(r);

        std::vector<size_t> min_w(cols, 1);
        auto mark_min = [&](const std::vector<std::string>& row) {
            for (size_t c = 0; c < row.size(); ++c) {
                size_t i = 0;
                while (i < row[c].size()) {
                    auto [cp, n] = DelimScanner::decode(row[c], i);
                    if (is_wide_cp(cp)) min_w[c] = 2;
                    i += n;
                }
            }
        };
        mark_min(f.headers);
        for (auto& r : f.rows) mark_min(r);
        const size_t overhead = 3 * cols + 2;
        size_t sum_min = 0;
        for (size_t x : min_w) sum_min += x;
        auto table_width = [&]() {
            size_t t = overhead;
            for (size_t x : w) t += x;
            return t;
        };
        bool too_narrow = false;
        if (max_width > 0 && table_width() > max_width) {
            if (max_width <= overhead || sum_min > max_width - overhead) {
                too_narrow = true;
            } else {
                std::vector<size_t> natural = w;
                size_t avail = max_width - overhead;
                w = min_w;
                size_t allocated = sum_min;
                while (allocated < avail) {
                    size_t mi = 0;
                    size_t best = natural[0] - w[0];
                    for (size_t c = 1; c < cols; ++c) {
                        size_t need = natural[c] - w[c];
                        if (need > best) {
                            best = need;
                            mi = c;
                        }
                    }
                    if (best == 0) break;
                    ++w[mi];
                    ++allocated;
                }
                too_narrow = table_width() > max_width;
            }
        }
        std::string out;
        if (too_narrow) {
            auto plain_line = [&](const std::vector<std::string>& row, bool header) {
                std::string joined;
                for (size_t c = 0; c < cols; ++c) {
                    if (c) joined += " | ";
                    if (c < row.size()) joined += row[c];
                }
                for (auto& ln : wrap_display(joined, max_width)) {
                    if (header) out += "\x1b[1m";
                    out += ln;
                    if (header) out += "\x1b[22m";
                    out += "\n";
                }
            };
            plain_line(f.headers, true);
            for (auto& r : f.rows) plain_line(r, false);
            return out;
        }

        auto pad = [](const std::string& s, size_t width, Align a) {
            size_t cur = display_width(s);
            if (cur >= width) return s;
            size_t d = width - cur;
            switch (a) {
                case Align::Right:  return std::string(d, ' ') + s;
                case Align::Center: {
                    size_t l = d / 2, r = d - l;
                    return std::string(l, ' ') + s + std::string(r, ' ');
                }
                default: return s + std::string(d, ' ');
            }
        };
        auto hline = [&](const char* l, const char* m, const char* r) {
            out += "\x1b[36m"; out += l;
            for (size_t c = 0; c < cols; ++c) {
                for (size_t k = 0; k < w[c] + 2; ++k) out += "\u2500";
                out += (c + 1 < cols) ? m : r;
            }
            out += "\x1b[0m\n";
        };
        auto row_line = [&](const std::vector<std::string>& row, bool header) {
            std::vector<std::vector<std::string>> cell_lines(cols);
            size_t height = 1;
            for (size_t c = 0; c < cols; ++c) {
                std::string cell = c < row.size() ? row[c] : "";
                cell_lines[c] = wrap_display(cell, w[c]);
                height = std::max(height, cell_lines[c].size());
            }
            for (size_t k = 0; k < height; ++k) {
                out += "\x1b[36m\u2502\x1b[0m ";
                for (size_t c = 0; c < cols; ++c) {
                    std::string cell = k < cell_lines[c].size()
                                     ? cell_lines[c][k] : "";
                    Align a = c < f.aligns.size() ? f.aligns[c] : Align::Left;
                    if (header) out += "\x1b[1m";
                    out += pad(cell, w[c], a);
                    if (header) out += "\x1b[22m";
                    out += " \x1b[36m\u2502\x1b[0m ";
                }
                out += "\n";
            }
        };
        hline("\u250c", "\u252c", "\u2510");
        row_line(f.headers, true);
        hline("\u251c", "\u253c", "\u2524");
        for (auto& r : f.rows) row_line(r, false);
        hline("\u2514", "\u2534", "\u2518");
        return out;
    }
    std::string render_fitted_table(const Frame& f) const {
        int view = line_cols_ > 0 ? line_cols_ : cols_;
        if (view <= 0) return render_table(f);
        size_t max_width = static_cast<size_t>(view);
        size_t prefix_width = display_width(current_prefix());
        max_width = (max_width > prefix_width) ? max_width - prefix_width : 0;
        return render_table(f, max_width);
    }
private:
    enum class Pending { Hold, Para, Block };
    std::vector<Frame> stack_;
    std::vector<Frame> containers_;
    RenderHooks        hooks_;
    std::string        line_prefix_;
    bool               line_started_ = false;
    std::string        cur_;
    bool               line_decided_ = false;
    bool               para_streaming_ = false;
    std::string        scan_pending_;
    bool               has_prev_ = false;
    char32_t           prev_cp_ = 0;
    std::ostream*      out_ = nullptr;
    std::string        out_default_;
    // ───────── 单行临时层状态：本行已显示的样子、应有的样子与所占行数 ─────────
    bool               tty_out_ = false;
    bool               provisional_ = true;
    int                cols_ = 0;
    int                rows_ = 0;
    int                line_cols_ = 0;
    bool               sl_open_ = false;
    size_t             sl_rows_ = 0;
    int                sl_col_ = 0;
    std::string        sl_shown_;
    std::string        sl_out_;
    std::string        sl_echo_;
    size_t             blk_rows_ = 0;
    bool               blk_committed_ = false;
    static Pending pending_of(const std::string& cur) {
        size_t i = 0;
        while (i < cur.size() && (cur[i] == ' ' || cur[i] == '\t')) ++i;
        if (i >= cur.size()) return Pending::Hold;
        char c = cur[i];
        switch (c) {
            case '>': return Pending::Block;
            case '#': {
                size_t j = i;
                while (j < cur.size() && cur[j] == '#') ++j;
                size_t n = j - i;
                if (n > 6) return Pending::Para;
                if (j >= cur.size()) return Pending::Hold;
                return (cur[j] == ' ' || cur[j] == '\t') ? Pending::Block : Pending::Para;
            }
            case '-': case '+': case '*': {
                if (i + 1 < cur.size() && (cur[i+1] == ' ' || cur[i+1] == '\t'))
                    return Pending::Block;
                size_t j = i;
                while (j < cur.size() && cur[j] == c) ++j;
                if (j >= cur.size()) return Pending::Hold;
                return Pending::Para;
            }
            case '`': {
                size_t j = i;
                while (j < cur.size() && cur[j] == '`') ++j;
                if (j - i >= 3) return Pending::Block;
                if (j >= cur.size()) return Pending::Hold;
                return Pending::Para;
            }
            case '|': return Pending::Block;
            case '$':
                if (i + 1 >= cur.size()) return Pending::Hold;
                return (cur[i+1] == '$') ? Pending::Block : Pending::Para;
            default: {
                if (!std::isdigit(static_cast<unsigned char>(c)))
                    return Pending::Para;
                size_t j = i;
                while (j < cur.size()
                       && std::isdigit(static_cast<unsigned char>(cur[j]))) ++j;
                if (j >= cur.size()) return Pending::Hold;
                if (cur[j] == '.' || cur[j] == ')') {
                    if (j + 1 >= cur.size()) return Pending::Hold;
                    if (cur[j+1] == ' ' || cur[j+1] == '\t') return Pending::Block;
                    return Pending::Para;
                }
                return Pending::Para;
            }
        }
    }
    void stream_chunk(const std::string& b) {
        sl_open_ = true;
        if (b.empty()) return;
        if (!stack_.empty() && stack_.back().layer == Layer::Protected) {
            cur_ += b;
            raw_echo(b);
            if (out_) out_->flush();
            return;
        }
        cur_ += b;
        raw_echo(b);
        if (!line_decided_) {
            Pending p = pending_of(cur_);
            if (p == Pending::Para) {
                line_decided_ = true;
                para_streaming_ = true;
                scan_pending_ = std::move(cur_);
                cur_.clear();
                scan_ready();
                if (out_) out_->flush();
                sl_apply();
            } else if (p == Pending::Block) {
                line_decided_ = true;
            }
            return;
        }
        if (para_streaming_) {
            scan_pending_ += b;
            scan_ready();
            if (out_) out_->flush();
            sl_apply();
        }
    }
    void scan_ready() {
        size_t keep = scan_pending_.size();
        while (keep > 0) {
            unsigned char c = static_cast<unsigned char>(scan_pending_[keep - 1]);
            if (c != '*' && c != '_' && c != '`' && c != '$') break;
            --keep;
        }
        if (keep > 0) {
            scan_inline(scan_pending_.substr(0, keep));
            scan_pending_.erase(0, keep);
        }
    }
    void end_current_line() {
        if (!scan_pending_.empty()) {
            scan_inline(scan_pending_);
            scan_pending_.clear();
        }
        if (para_streaming_) {
            flush_line();
        } else if (cur_.empty()) {
            feed("");
        } else if (!stack_.empty() && stack_.back().layer == Layer::Protected) {
            feed(cur_);
        } else {
            feed(cur_);
        }
        cur_.clear();
        scan_pending_.clear();
        line_decided_ = false;
        para_streaming_ = false;
        has_prev_ = false;
        prev_cp_  = 0;
    }
    bool has_inline_frame() const {
        for (const auto& f : stack_)
            if (f.layer == Layer::Inline) return true;
        return false;
    }
    std::string strip_protected_prefix(const std::string& line) const {
        int quote_depth = 0;
        for (const auto& f : containers_)
            if (f.delim == Delim::Quote) ++quote_depth;
        size_t i = 0;
        for (int q = 0; q < quote_depth; ++q) {
            size_t j = i;
            while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) ++j;
            if (j < line.size() && line[j] == '>') {
                ++j;
                if (j < line.size() && line[j] == ' ') ++j;
                i = j;
            } else {
                break;
            }
        }
        return line.substr(i);
    }
    std::string strip_container(const std::string& line) {
        line_prefix_.clear();
        int quote_depth = 0;
        size_t i = 0;
        while (i < line.size()) {
            size_t j = i;
            while (j < line.size() && (line[j] == ' ' || line[j] == '\t')) ++j;
            if (j < line.size() && line[j] == '>') {
                ++quote_depth;
                ++j;
                if (j < line.size() && line[j] == ' ') ++j;
                i = j;
            } else break;
        }
        size_t indent_start = i;
        while (i < line.size() && line[i] == ' ') ++i;
        int list_indent = static_cast<int>((i - indent_start) / 2);
        bool is_list = false, ordered = false;
        size_t content_start = i;
        if (i < line.size()) {
            char c = line[i];
            if ((c == '-' || c == '*' || c == '+')
                && i + 1 < line.size()
                && (line[i+1] == ' ' || line[i+1] == '\t')) {
                is_list = true;
                content_start = i + 2;
                while (content_start < line.size() && line[content_start] == ' ')
                    ++content_start;
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                size_t k = i;
                while (k < line.size()
                       && std::isdigit(static_cast<unsigned char>(line[k]))) ++k;
                if (k < line.size() && (line[k] == '.' || line[k] == ')')
                    && k + 1 < line.size()
                    && (line[k+1] == ' ' || line[k+1] == '\t')) {
                    is_list = true;
                    ordered = true;
                    content_start = k + 2;
                    while (content_start < line.size() && line[content_start] == ' ')
                        ++content_start;
                }
            }
        }
        int cur_quote = 0;
        for (auto& f : containers_) if (f.delim == Delim::Quote) ++cur_quote;
        while (cur_quote > quote_depth) {
            for (size_t k = containers_.size(); k-- > 0; ) {
                if (containers_[k].delim == Delim::Quote) {
                    containers_.erase(containers_.begin() + k);
                    break;
                }
            }
            --cur_quote;
        }
        while (cur_quote < quote_depth) {
            Frame f;
            f.delim = Delim::Quote;
            f.layer = Layer::Container;
            f.indent = ++cur_quote;
            containers_.push_back(std::move(f));
        }
        if (is_list) {
            Frame* match = nullptr;
            for (auto& f : containers_) {
                if (f.delim == Delim::List && f.indent == list_indent) match = &f;
            }
            if (!match) {
                containers_.erase(
                    std::remove_if(containers_.begin(), containers_.end(),
                        [&](const Frame& f) {
                            return f.delim == Delim::List && f.indent >= list_indent;
                        }),
                    containers_.end());
                Frame f;
                f.delim = Delim::List;
                f.layer = Layer::Container;
                f.indent = list_indent;
                f.ordered = ordered;
                f.index = 1;
                containers_.push_back(std::move(f));
                match = &containers_.back();
            }
            if (match->ordered) {
                line_prefix_ = std::to_string(match->index) + ". ";
                match->index++;
            } else {
                line_prefix_ = "- ";
            }
        }
        if (content_start >= line.size()) return "";
        return line.substr(content_start);
    }
    void handle_math_display(const std::string& line, bool opening) {
        Frame& top = stack_.back();
        size_t cur = 0;
        size_t total = blk_rows_;
        if (!opening) {
            cur = line_take();
            total = blk_rows_ + cur;
            if (sl_active() && total > max_blk_rows()) blk_committed_ = true;
        }

        std::string body = (opening && line.size() >= 2) ? line.substr(2) : line;
        std::string before;
        std::string tail;
        bool close = false;
        if (opening) {
            size_t c = body.find("$$");
            if (c != std::string::npos) {
                before = body.substr(0, c);
                tail   = body.substr(c + 2);
                close  = true;
            } else {
                before = body;
            }
        } else {
            size_t p = line.find_first_not_of(" \t");
            if (p != std::string::npos && line.compare(p, 2, "$$") == 0) {
                before = line.substr(0, p);
                tail   = line.substr(p + 2);
                close  = true;
            } else {
                before = line;
            }
        }

        auto append_line = [&](const std::string& piece) {
            if (!top.buf.empty()) top.buf += '\n';
            top.buf += piece;
        };

        if (close) {
            if (!before.empty() && !trim(before).empty())
                append_line(before);
            Frame f = std::move(top);
            stack_.pop_back();
            bool degraded = sl_active() && blk_committed_;
            if (!degraded) {
                if (sl_active()) blk_erase(total);
                line_started_ = false;
                emit_block(hooks_.render ? hooks_.render(f) : f.buf);
            }
            blk_rows_      = 0;
            blk_committed_ = false;
            if (!trim(tail).empty()) feed(tail);
            return;
        }

        if (!opening || !before.empty())
            append_line(before);
        blk_rows_ += cur;
    }
    void handle_protected(const std::string& line,
                          const std::string& raw_line = std::string()) {
        size_t cur = line_take();
        size_t total = blk_rows_ + cur;
        if (sl_active() && total > max_blk_rows()) blk_committed_ = true;
        bool degraded = sl_active() && blk_committed_;
        Frame& top = stack_.back();
        if (top.delim == Delim::Table) {
            std::string t = trim(line);
            if (t.empty() || !is_table_row(t)) {
                Frame f = std::move(top);
                stack_.pop_back();
                if (sl_active())
                    blk_erase(degraded ? cur : total);
                if (!degraded) {
                    if (!f.header_done) {
                        emit(plain_table(f));
                    } else {
                        emit_block(render_fitted_table(f));
                    }
                }
                blk_rows_      = 0;
                blk_committed_ = false;
                const std::string& restore = raw_line.empty() ? line : raw_line;
                line_restore(restore);
                feed(restore);
                return;
            }
            blk_rows_ += cur;
            if (sl_active() && blk_rows_ > max_blk_rows()) blk_committed_ = true;
            auto cells = split_row(t);
            if (!top.header_done) {
                if (is_separator_row(cells)) {
                    top.aligns = parse_aligns(cells);
                    top.header_done = true;
                } else if (!top.header_set) {
                    top.headers = cells;
                } else {
                    top.rows.push_back(cells);
                }
                top.header_set = true;
            } else {
                top.rows.push_back(cells);
            }
            return;
        }
        const std::string& close = top.literal;
        size_t p = line.find_first_not_of(" \t");
        bool is_close = (p != std::string::npos
                         && line.compare(p, close.size(), close) == 0);
        if (is_close) {
            std::string tail = line.substr(p + close.size());
            Frame f = std::move(top);
            stack_.pop_back();
            if (!degraded) {
                if (sl_active()) blk_erase(total);
                line_started_ = false;
                emit_block(hooks_.render ? hooks_.render(f) : f.buf);
            }
            blk_rows_      = 0;
            blk_committed_ = false;
            if (!trim(tail).empty()) {
                std::string tail_line = tail;
                if (!raw_line.empty() && raw_line.size() >= tail.size())
                    tail_line = raw_line.substr(raw_line.size() - tail.size());
                line_restore(tail_line);
                feed(tail_line);
            }
            return;
        }
        blk_rows_ += cur;
        if (sl_active() && blk_rows_ > max_blk_rows()) blk_committed_ = true;
        top.buf += line;
        top.buf += '\n';
    }
    void scan_inline(const std::string& s) {
        size_t i = 0;
        while (i < s.size()) {
            if (s[i] == '\\' && i + 1 < s.size()) {
                auto [ecp, en] = DelimScanner::decode(s, i + 1);
                emit(s.substr(i, 1 + en));
                i += 1 + en;
                has_prev_ = true;
                prev_cp_  = ecp;
                continue;
            }
            auto [cp, n] = DelimScanner::decode(s, i);
            bool in_word = false;
            size_t next = i + n;
            if (next < s.size()) {
                bool prev_word;
                if (i == 0) {
                    prev_word = has_prev_
                        && !DelimScanner::cp_is_space(prev_cp_)
                        && !DelimScanner::cp_is_punct(prev_cp_);
                } else {
                    size_t k = i;
                    while (k > 0 && (s[k - 1] & 0xC0) == 0x80) --k;
                    prev_word = false;
                    if (k == 0) {
                        prev_word = has_prev_
                            && !DelimScanner::cp_is_space(prev_cp_)
                            && !DelimScanner::cp_is_punct(prev_cp_);
                    } else {
                        auto [pcp, pn] = DelimScanner::decode(s, k - 1);
                        (void)pn;
                        prev_word = !DelimScanner::cp_is_space(pcp)
                                 && !DelimScanner::cp_is_punct(pcp);
                    }
                }
                auto [ncp, nn] = DelimScanner::decode(s, next);
                (void)nn;
                bool next_word = !DelimScanner::cp_is_space(ncp)
                              && !DelimScanner::cp_is_punct(ncp);
                in_word = prev_word && next_word;
            }
            auto m = DelimScanner::match(s, i, in_word, has_prev_, prev_cp_);
            if (!m) {
                emit(s.substr(i, n));
                i += n;
                has_prev_ = true;
                prev_cp_  = cp;
                continue;
            }
            if (!stack_.empty() && stack_.back().layer == Layer::Inline) {
                Delim top_d = stack_.back().delim;
                if ((top_d == Delim::Code || top_d == Delim::MathInline)
                    && m->kind != top_d) {
                    emit(m->literal);
                    i += m->len;
                    has_prev_ = true;
                    prev_cp_  = cp;
                    continue;
                }
            }
            if (!stack_.empty()
                && stack_.back().layer == Layer::Inline
                && stack_.back().delim == m->kind
                && (m->kind != Delim::MathInline
                    || stack_.back().literal == m->literal)
                && m->can_close) {
                Frame f = std::move(stack_.back());
                stack_.pop_back();
                emit(hooks_.render ? hooks_.render(f) : f.buf);
            } else if (m->can_open) {
                Frame f;
                f.delim   = m->kind;
                f.layer   = Layer::Inline;
                f.literal = m->literal;
                stack_.push_back(std::move(f));
            } else {
                emit(m->literal);
            }
            i += m->len;
            has_prev_ = true;
            prev_cp_  = cp;
        }
    }
    void emit(const std::string& s) {
        for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
            if (it->layer == Layer::Inline) {
                it->buf += s;
                return;
            }
        }
        if (sl_open_ && sl_active()) {
            sl_out_ += s;
            return;
        }
        ensure_prefix();
        emit_raw(s);
    }
    void emit_block(const std::string& s) {
        std::string prefix = current_prefix();
        if (prefix.empty()) {
            emit(s);
            return;
        }
        if (s.empty()) {
            ensure_prefix();
            line_started_ = true;
            return;
        }
        std::string block = s;
        size_t last_nl = block.rfind('\n');
        if (last_nl != std::string::npos && last_nl + 1 < block.size()) {
            std::string tail = block.substr(last_nl + 1);
            size_t prev_nl = (last_nl == 0)
                           ? std::string::npos
                           : block.rfind('\n', last_nl - 1);
            bool last_line_empty = (prev_nl != std::string::npos
                                    && last_nl == prev_nl + 1);
            if (!has_visible(tail) && !last_line_empty)
                block = block.substr(0, last_nl) + tail + "\n";
        }
        size_t start = 0;
        while (start < block.size()) {
            size_t nl = block.find('\n', start);
            if (nl == std::string::npos) {
                std::string tail = block.substr(start);
                if (has_visible(tail))
                    emit_raw(prefix + tail);
                else
                    emit_raw(tail);
                line_started_ = false;
                return;
            }
            emit_raw(prefix + block.substr(start, nl - start));
            emit_raw("\n");
            start = nl + 1;
            line_started_ = false;
        }
    }
    void ensure_prefix() {
        if (line_started_) return;
        line_started_ = true;
        if (sl_open_ && sl_active()) return;
        std::string prefix = current_prefix();
        if (!prefix.empty()) emit_raw(prefix);
    }
    std::string current_prefix() const {
        std::string prefix;
        for (const auto& f : containers_)
            if (f.delim == Delim::Quote) prefix += "\u2502 ";
        prefix += line_prefix_;
        return prefix;
    }
    // ───────── 单行临时层：进来的字节先原样出，行确定后擦掉重画；只在真终端生效 ─────────
    static bool fd_is_tty(int fd) {
#if defined(__unix__) || defined(__APPLE__)
        return ::isatty(fd) == 1;
#else
        (void)fd;
        return false;
#endif
    }
    static int fd_terminal_cols(int fd) {
        if (const char* c = std::getenv("COLUMNS")) {
            int v = std::atoi(c);
            if (v > 0) return v;
        }
#if defined(TIOCGWINSZ)
        struct winsize ws{};
        if (::ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
            return static_cast<int>(ws.ws_col);
#else
        (void)fd;
#endif
        return 0;
    }
    static int fd_terminal_rows(int fd) {
        if (const char* c = std::getenv("LINES")) {
            int v = std::atoi(c);
            if (v > 0) return v;
        }
#if defined(TIOCGWINSZ)
        struct winsize ws{};
        if (::ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
            return static_cast<int>(ws.ws_row);
#else
        (void)fd;
#endif
        return 0;
    }
    bool sl_active() const { return provisional_active() && line_cols_ > 0; }
    size_t max_blk_rows() const {
        int h = rows_ > 0 ? rows_ : fd_terminal_rows(1);
        return h > 1 ? static_cast<size_t>(h - 1) : 0;
    }
    void line_put(const std::string& s) {
        if (s.empty()) return;
        emit_raw(s);
        sl_shown_ += s;
        const int cols = line_cols_ > 0 ? line_cols_ : 80;
        size_t i = 0;
        while (i < s.size()) {
            auto [cp, n] = DelimScanner::decode(s, i);
            i += n;
            if (cp == 0x1b) {
                if (i < s.size() && s[i] == '[') {
                    ++i;
                    while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) ++i;
                    if (i < s.size()) ++i;
                } else if (i < s.size()) {
                    ++i;
                }
                continue;
            }
            int w;
            if (cp == '\t') {
                int next = ((sl_col_ / 8) + 1) * 8;
                w = next - sl_col_;
                if (sl_rows_ > 0 && sl_col_ + w > cols) { sl_rows_++; sl_col_ = 0; w = 8; }
                if (w <= 0) w = 1;
            } else if (cp == '\n') {
                sl_rows_ = sl_rows_ > 0 ? sl_rows_ + 1 : 1;
                sl_col_  = 0;
                continue;
            } else if (cp >= 0x20 && cp != 0x7F) {
                w = is_wide_cp(cp) ? 2 : 1;
            } else {
                continue;
            }
            if (sl_rows_ == 0)                       sl_rows_ = 1;
            else if (sl_col_ + w > cols)           { sl_rows_++; sl_col_ = 0; }
            sl_col_ += w;
            if (sl_col_ > cols) sl_col_ = cols;
        }
    }
    static size_t utf8_complete_len(const std::string& s) {
        size_t n = s.size(), i = n;
        while (i > 0 && (static_cast<unsigned char>(s[i-1]) & 0xC0) == 0x80) --i;
        if (i == 0) return n;
        unsigned char lead = static_cast<unsigned char>(s[i-1]);
        size_t need = ((lead >> 5) == 0x6) ? 2
                    : ((lead >> 4) == 0xE) ? 3
                    : ((lead >> 3) == 0x1E) ? 4 : 1;
        size_t have = n - (i - 1);
        return (need > have) ? i - 1 : n;
    }
    void raw_echo(const std::string& b) {
        if (!sl_active()) return;
        sl_echo_ += b;
        size_t k = utf8_complete_len(sl_echo_);
        if (k) {
            line_put(sl_echo_.substr(0, k));
            sl_echo_.erase(0, k);
        }
    }
    void echo_flush() {
        if (sl_echo_.empty()) return;
        line_put(sl_echo_);
        sl_echo_.clear();
    }
    std::string line_want() const {
        std::string pending;
        for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
            if (it->layer != Layer::Inline) break;
            pending = it->literal + it->buf + pending;
        }
        return current_prefix() + sl_out_ + pending + scan_pending_;
    }
    void line_erase(size_t up) {
        emit_raw("\r");
        if (up) emit_raw("\x1b[" + std::to_string(up) + "A");
        emit_raw("\x1b[J");
        if (out_) out_->flush();
    }
    bool sl_apply(bool strip_tail = true) {
        if (!sl_open_ || !sl_active()) return false;
        std::string want = line_want();
        if (strip_tail) want.erase(utf8_complete_len(want));
        if (want == sl_shown_) return false;
        line_erase(sl_rows_ > 0 ? sl_rows_ - 1 : 0);
        sl_rows_ = 0;
        sl_col_  = 0;
        sl_shown_.clear();
        line_put(want);
        return true;
    }
    void sl_reset() {
        sl_echo_.clear();
        sl_open_      = false;
        sl_rows_      = 0;
        sl_col_       = 0;
        sl_shown_.clear();
        sl_out_.clear();
        line_started_ = false;
    }
    void line_restore(const std::string& text) {
        if (!sl_active()) return;
        sl_open_ = true;
        sl_rows_ = 0;
        sl_col_  = 0;
        sl_shown_.clear();
        sl_out_.clear();
        line_put(text);
    }
    size_t line_take() {
        if (!sl_open_) return 0;
        size_t rows = sl_rows_ > 0 ? sl_rows_ : 1;
        if (sl_active()) emit_raw("\n");
        sl_reset();
        return rows;
    }
    void blk_erase(size_t up) {
        line_erase(up);
        line_started_ = false;
    }
    void flush_line() {
        force_close_inline();
        if (!sl_open_ || !sl_active()) {
            ensure_prefix();
            emit_raw("\n");
            if (out_) out_->flush();
            sl_reset();
            line_started_ = false;
            line_prefix_.clear();
            return;
        }
        echo_flush();
        sl_apply(/*strip_tail=*/false);
        emit_raw("\n");
        if (out_) out_->flush();
        sl_reset();
        line_prefix_.clear();
    }
    void emit_raw(const std::string& s) {
        if (out_) *out_ << s;
        else      out_default_ += s;
    }
    void force_close_inline() {
        std::string out;
        while (!stack_.empty() && stack_.back().layer == Layer::Inline) {
            Frame f = std::move(stack_.back());
            stack_.pop_back();
            out = f.literal + f.buf + out;
        }
        if (!out.empty()) emit(out);
        has_prev_ = false;
        prev_cp_  = 0;
    }
};
inline StreamRenderer::StreamRenderer() : hooks_(default_hooks()) {}

// ───────── 默认钩子：表格、边框与转义字符的默认实现 ─────────
inline RenderHooks default_hooks() {
    RenderHooks h;
    h.try_block_leaf = [](const std::string& s) -> std::optional<std::string> {
        size_t n = 0;
        while (n < s.size() && s[n] == '#') ++n;
        if (n >= 1 && n <= 6 && n < s.size() && s[n] == ' ') {
            return std::string("\x1b[1;33m") + s.substr(n + 1) + "\x1b[0m";
        }
        if (s == "---" || s == "***" || s == "___")
            return std::string("\x1b[36m\u2500\u2500\u2500\u2500\u2500\u2500\x1b[0m");
        return std::nullopt;
    };
    h.try_block_open = [](const std::string& s) -> std::optional<Frame> {
        if (s.size() >= 3 && s.compare(0, 3, "```") == 0) {
            Frame f; f.delim = Delim::Fence;
            f.layer = Layer::Protected; f.literal = "```";
            f.aux = s.substr(3);
            return f;
        }
        if (s.size() >= 2 && s.compare(0, 2, "$$") == 0) {
            Frame f; f.delim = Delim::MathDisplay;
            f.layer = Layer::Protected; f.literal = "$$";
            return f;
        }
        if (!s.empty() && s.front() == '|' && StreamRenderer::is_table_row(s)) {
            Frame f; f.delim = Delim::Table;
            f.layer = Layer::Protected; f.literal = "|";
            return f;
        }
        return std::nullopt;
    };
    h.render = [](const Frame& f) -> std::string {
        switch (f.delim) {
            case Delim::Bold:        return "\x1b[1m" + f.buf + "\x1b[22m";
            case Delim::Italic:      return "\x1b[3m" + f.buf + "\x1b[23m";
            case Delim::BoldItalic:  return "\x1b[1;3m" + f.buf + "\x1b[22;23m";
            case Delim::Code:        return "\x1b[36m" + f.buf + "\x1b[39m";
            case Delim::MathInline:  return "\x1b[35m" + f.buf + "\x1b[39m";
            case Delim::Fence:       return "\x1b[32m" + f.buf + "\x1b[39m";
            case Delim::MathDisplay: return "\x1b[35m" + f.buf + "\x1b[39m\n";
            case Delim::Table:       return StreamRenderer::render_table(f);
            default:                 return f.buf;
        }
    };
    return h;
}
}
