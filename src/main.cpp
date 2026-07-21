//============================================================================
// listtree - 在终端中美观地显示目录树结构（支持实时监控 + 交互模式）
// 用法: listtree [目录路径] [-r|--recursive] [-a|--all] [-w|--watch]
//        listtree -i [-p pane_id] [-c cd_pane_id] [目录路径]  # 交互模式
//============================================================================

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#include <fmt/core.h>
#include <fmt/color.h>

namespace fs = std::filesystem;

//============================================================================
// 颜色与样式定义
//============================================================================
namespace style {

inline constexpr auto dir_color     = fmt::fg(fmt::color::deep_sky_blue) | fmt::emphasis::bold;
inline constexpr auto file_color    = fmt::fg(fmt::color::light_gray);
inline constexpr auto exec_color    = fmt::fg(fmt::color::lime_green) | fmt::emphasis::bold;
inline constexpr auto link_color    = fmt::fg(fmt::color::cyan) | fmt::emphasis::italic;
inline constexpr auto hidden_color  = fmt::fg(fmt::color::dim_gray) | fmt::emphasis::italic;
inline constexpr auto size_color    = fmt::fg(fmt::color::gold);
inline constexpr auto date_color    = fmt::fg(fmt::color::wheat);
inline constexpr auto stats_color   = fmt::fg(fmt::color::yellow) | fmt::emphasis::bold;
inline constexpr auto error_color   = fmt::fg(fmt::color::red) | fmt::emphasis::bold;
inline constexpr auto line_color    = fmt::fg(fmt::color::steel_blue);
inline constexpr auto title_color   = fmt::fg(fmt::color::hot_pink) | fmt::emphasis::bold;
inline constexpr auto watch_color   = fmt::fg(fmt::color::green_yellow) | fmt::emphasis::bold;
inline constexpr auto change_color  = fmt::fg(fmt::color::aqua);
inline constexpr auto info_color    = fmt::fg(fmt::color::yellow);

} // namespace style

//============================================================================
// 全局状态
//============================================================================
static volatile bool g_running = true;

void signal_handler(int) { g_running = false; }

//============================================================================
// 工具函数
//============================================================================

bool is_executable(const fs::directory_entry &entry) {
    if (!entry.is_regular_file()) return false;
    auto perms = entry.status().permissions();
    return (perms & fs::perms::owner_exec) != fs::perms::none ||
           (perms & fs::perms::group_exec) != fs::perms::none ||
           (perms & fs::perms::others_exec) != fs::perms::none;
}

bool is_hidden(const fs::path &p) {
    auto filename = p.filename().string();
    return !filename.empty() && filename[0] == '.';
}

std::string format_size(uintmax_t bytes) {
    constexpr const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && unit_idx < 4) { size /= 1024.0; ++unit_idx; }
    if (unit_idx == 0) return fmt::format("{} {}", bytes, units[unit_idx]);
    return fmt::format("{:.1f} {}", size, units[unit_idx]);
}

std::string format_time(const fs::file_time_type &ftime) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    auto tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm *tm = std::localtime(&tt);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
    return std::string(buf);
}

std::string_view file_icon(const fs::directory_entry &entry) {
    if (entry.is_directory()) return "📁";
    if (entry.is_symlink()) return "🔗";
    auto ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".cpp" || ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".cc" || ext == ".cxx") return "🅲";
    if (ext == ".py") return "🐍";
    if (ext == ".java") return "☕";
    if (ext == ".rs") return "🦀";
    if (ext == ".go") return "🔵";
    if (ext == ".js" || ext == ".ts" || ext == ".mjs") return "🟨";
    if (ext == ".html" || ext == ".htm") return "🌐";
    if (ext == ".css" || ext == ".scss" || ext == ".less") return "🎨";
    if (ext == ".json") return "📋";
    if (ext == ".xml") return "📄";
    if (ext == ".yaml" || ext == ".yml") return "⚙️";
    if (ext == ".md" || ext == ".markdown") return "📝";
    if (ext == ".txt") return "📃";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp" || ext == ".webp") return "🖼️";
    if (ext == ".svg") return "✏️";
    if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".ogg") return "🎵";
    if (ext == ".mp4" || ext == ".avi" || ext == ".mkv" || ext == ".mov") return "🎬";
    if (ext == ".zip" || ext == ".tar" || ext == ".gz" || ext == ".bz2" || ext == ".7z" || ext == ".rar") return "📦";
    if (ext == ".pdf") return "📕";
    if (ext == ".sh" || ext == ".bash" || ext == ".zsh") return "⚡";
    if (ext == ".so" || ext == ".dll" || ext == ".dylib") return "🔧";
    if (ext == ".o" || ext == ".obj") return "🧱";
    if (ext == ".exe" || ext == ".out") return "▶️";
    if (ext == ".toml") return "🔩";
    if (ext == ".lock") return "🔒";
    if (ext == ".gitignore" || ext == ".gitattributes" || ext == ".gitmodules") return "🙈";
    if (ext == ".env") return "🔐";
    if (ext == ".config" || ext == ".conf" || ext == ".ini") return "⚙️";
    if (ext == ".log") return "📋";
    if (ext == ".csv") return "📊";
    if (ext == ".sql" || ext == ".db" || ext == ".sqlite") return "🗄️";
    if (ext == ".vim" || ext == ".vimrc") return "✏️";
    if (ext == ".dockerfile" || ext == "dockerfile") return "🐳";
    if (is_executable(entry)) return "▶️";
    return "  ";
}

// Check if file is a document type (text/code) that can be opened with micro
bool is_document_file(const fs::path &p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    const std::vector<std::string> doc_exts = {
        ".txt", ".md", ".markdown", ".cpp", ".c", ".h", ".hpp", ".cc", ".cxx",
        ".py", ".java", ".rs", ".go", ".js", ".ts", ".mjs", ".html", ".htm",
        ".css", ".scss", ".less", ".json", ".xml", ".yaml", ".yml",
        ".toml", ".ini", ".cfg", ".conf", ".sh", ".bash", ".zsh",
        ".vim", ".vimrc", ".dockerfile", ".cmake", ".mk", ".sql",
        ".csv", ".log", ".env", ".gitignore", ".gitattributes",
        ".gitmodules", ".editorconfig", ".svg", ".tex", ".bib",
        ".lua", ".pl", ".pm", ".rb", ".php", ".swift", ".kt", ".scala",
        ".clj", ".cljs", ".elm", ".hs", ".ml", ".mli", ".erl", ".hrl",
        ".ex", ".exs", ".tsx", ".jsx", ".vue", ".svelte", ".astro",
        ".makefile", "makefile", ".dockerfile"
    };
    return std::find(doc_exts.begin(), doc_exts.end(), ext) != doc_exts.end() ||
           std::find(doc_exts.begin(), doc_exts.end(), p.filename().string()) != doc_exts.end();
}

// Check if file is an archive
bool is_archive_file(const fs::path &p) {
    auto ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    const std::vector<std::string> archive_exts = {
        ".zip", ".tar", ".gz", ".bz2", ".xz", ".7z", ".rar",
        ".tgz", ".tbz2", ".txz", ".tar.gz", ".tar.bz2", ".tar.xz"
    };
    return std::find(archive_exts.begin(), archive_exts.end(), ext) != archive_exts.end();
}
//============================================================================
// 树形数据结构 - 用于交互模式
//============================================================================

struct TreeNode {
    fs::path path;
    std::string display_name;
    std::string icon_str;
    bool is_dir;
    bool is_sym;
    bool is_hid;
    bool is_exec;
    bool expanded = false;      // 是否展开（仅目录有效）
    int depth = 0;              // 缩进层级
    uintmax_t fsize = 0;
    std::string size_str;
    std::string time_str;
    TreeNode() = default;

    TreeNode(const fs::directory_entry &entry, int depth_level)
        : path(entry.path())
        , display_name(entry.path().filename().string())
        , icon_str(file_icon(entry))
        , is_dir(entry.is_directory())
        , is_sym(entry.is_symlink())
        , is_hid(is_hidden(entry.path()))
        , is_exec(is_executable(entry))
        , depth(depth_level)
    {
        if (entry.is_regular_file()) {
            std::error_code ec;
            fsize = entry.file_size(ec);
            if (!ec) size_str = format_size(fsize);
        }
        try {
            auto ftime = entry.last_write_time();
            time_str = format_time(ftime);
        } catch (...) {}
    }
};

//============================================================================
// 获取终端大小
//============================================================================

struct TermSize {
    int rows = 24;
    int cols = 80;
};

TermSize get_term_size() {
    TermSize ts;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        ts.rows = ws.ws_row;
        ts.cols = ws.ws_col;
    }
    return ts;
}
//============================================================================
// 终端原始模式（用于捕获按键）
//============================================================================

struct termios orig_termios;

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; // 100ms timeout
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

//============================================================================
// 读取按键（支持方向键等转义序列 + 输入模式）
//============================================================================

enum class Key {
    UP, DOWN, LEFT, RIGHT,
    SPACE, ENTER, ESC, Q,
    MKDIR,      // m 键：创建目录
    NEWFILE,    // n 键：创建文件
    DELETE,     // d 键：递归删除
    CDROOT,     // u 键：cd 到最上层目录
    UNKNOWN
};

// 读取一行输入（在原始模式下，读取直到 Enter）
std::string read_line_input() {
    std::string result;
    char c;
    // 临时改为阻塞模式
    struct termios raw;
    tcgetattr(STDIN_FILENO, &raw);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    while (true) {
        if (read(STDIN_FILENO, &c, 1) != 1) break;
        if (c == '\n' || c == '\r') break;
        if (c == 127 || c == '\b') { // 退格
            if (!result.empty()) {
                result.pop_back();
                // 回退并清除字符
                fmt::print("\b \b");
                fflush(stdout);
            }
            continue;
        }
        if (c >= 32 && c <= 126) { // 可打印字符
            result += c;
            fmt::print("{}", c);
            fflush(stdout);
        }
    }

    // 恢复非阻塞模式
    {
        struct termios restore;
        tcgetattr(STDIN_FILENO, &restore);
        restore.c_cc[VMIN] = 0;
        restore.c_cc[VTIME] = 1;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &restore);
    }

    return result;
}

Key read_key() {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return Key::UNKNOWN;

    if (c == '\033') {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return Key::ESC;
        if (seq[0] == '[') {
            if (read(STDIN_FILENO, &seq[1], 1) != 1) return Key::UNKNOWN;
            switch (seq[1]) {
                case 'A': return Key::UP;
                case 'B': return Key::DOWN;
                case 'C': return Key::RIGHT;
                case 'D': return Key::LEFT;
                default: return Key::UNKNOWN;
            }
        }
        return Key::UNKNOWN;
    }

    if (c == ' ') return Key::SPACE;
    if (c == '\n' || c == '\r') return Key::ENTER;
    if (c == 'q' || c == 'Q') return Key::Q;
    if (c == 'm' || c == 'M') return Key::MKDIR;
    if (c == 'n' || c == 'N') return Key::NEWFILE;
    if (c == 'd' || c == 'D') return Key::DELETE;
    if (c == 'u' || c == 'U') return Key::CDROOT;
    if (c == 27) return Key::ESC;

    return Key::UNKNOWN;
}

//============================================================================
// 渲染完整交互界面
//============================================================================

void render_interactive(const std::vector<TreeNode> &nodes, int cursor_pos,
                        const fs::path &root_path, const std::vector<std::string> &pane_ids,
                        const std::vector<std::string> &cd_pane_ids,
                        const std::string &status_msg,
                        int &out_scroll_offset) {
    auto ts = get_term_size();

    // 清屏
    fmt::print("\033[2J\033[H");

    // 标题行
    fmt::print(style::title_color, "  📂 交互模式: ");
    fmt::print(fg(fmt::color::white) | fmt::emphasis::bold, "{}", root_path.string());
    if (!pane_ids.empty()) {
        std::string pane_str;
        for (size_t i = 0; i < pane_ids.size(); ++i) {
            if (i > 0) pane_str += ",";
            pane_str += pane_ids[i];
        }
        fmt::print(style::info_color, "  [tmux: {}]", pane_str);
    }
    if (!cd_pane_ids.empty()) {
        std::string cd_str;
        for (size_t i = 0; i < cd_pane_ids.size(); ++i) {
            if (i > 0) cd_str += ",";
            cd_str += cd_pane_ids[i];
        }
        fmt::print(style::info_color, "  [cd: {}]", cd_str);
    }
    fmt::print("\n");

    // 分隔线
    fmt::print(style::line_color, "  ");
    for (int i = 0; i < std::min(ts.cols - 2, 60); ++i) fmt::print("─");
    fmt::print("\n");

    // 计算可见行数（减去标题、分隔线、底部提示）
    int available_rows = ts.rows - 5;
    if (available_rows <= 0) available_rows = 10;

    // 计算滚动偏移
    int scroll_offset = 0;
    if (cursor_pos >= available_rows) {
        scroll_offset = cursor_pos - available_rows + 1;
    }
    if (cursor_pos < scroll_offset) {
        scroll_offset = cursor_pos;
    }
    out_scroll_offset = scroll_offset;

    // 渲染可见节点
    int start_idx = scroll_offset;
    int end_idx = std::min(start_idx + available_rows, (int)nodes.size());

    for (int i = start_idx; i < end_idx; ++i) {
        const auto &node = nodes[i];
        bool is_cursor = (i == cursor_pos);

        std::string indent;
        for (int d = 0; d < node.depth; ++d) {
            indent += "│   ";
        }

        std::string line;
        line += indent;
        line += "├── ";
        line += node.icon_str + " ";

        std::string name_part = node.display_name;
        if (node.is_dir && node.expanded) {
            name_part = "▼ " + name_part;
        }

        line += name_part;

        if (!node.size_str.empty() || !node.time_str.empty()) {
            line += " ·";
            if (!node.size_str.empty()) { line += " " + node.size_str; }
            if (!node.time_str.empty()) { line += " " + node.time_str; }
        }

        if (is_cursor) {
            fmt::print("\033[7m{}\033[0m\n", line);
        } else {
            fmt::print("{}\n", line);
        }
    }

    // 底部状态栏
    fmt::print(style::line_color, "  ");
    for (int i = 0; i < std::min(ts.cols - 2, 60); ++i) fmt::print("─");
    fmt::print("\n");

    if (!status_msg.empty()) {
        fmt::print(style::info_color, "  {}\n", status_msg);
    } else {
        fmt::print(style::watch_color,
            "  ↑↓ 移动  Space:展开/打开  q:退出  ({} / {})\n",
            cursor_pos + 1, nodes.size());
    }
}
//============================================================================
// 在指定 tmux 窗格中执行命令（支持多个窗格，逗号分隔）
//============================================================================

// 将逗号分隔的窗格 ID 字符串转为 vector
std::vector<std::string> parse_pane_ids(const std::string &ids) {
    std::vector<std::string> result;
    if (ids.empty()) return result;
    size_t start = 0, end;
    while ((end = ids.find(',', start)) != std::string::npos) {
        std::string id = ids.substr(start, end - start);
        // 去除首尾空格
        while (!id.empty() && id.front() == ' ') id.erase(0, 1);
        while (!id.empty() && id.back() == ' ') id.pop_back();
        if (!id.empty()) result.push_back(id);
        start = end + 1;
    }
    std::string id = ids.substr(start);
    while (!id.empty() && id.front() == ' ') id.erase(0, 1);
    while (!id.empty() && id.back() == ' ') id.pop_back();
    if (!id.empty()) result.push_back(id);
    return result;
}

void tmux_send_keys(const std::vector<std::string> &pane_ids, const std::string &cmd) {
    if (pane_ids.empty()) return;
    std::string escaped_cmd = cmd;
    // 转义单引号
    size_t pos = 0;
    while ((pos = escaped_cmd.find('\'', pos)) != std::string::npos) {
        escaped_cmd.replace(pos, 1, "'\\''");
        pos += 4;
    }
    for (const auto &pane_id : pane_ids) {
        std::string full_cmd = fmt::format("tmux send-keys -t '{}' '{}' Enter", pane_id, escaped_cmd);
        std::system(full_cmd.c_str());
    }
}

// 发送命令但不带 Enter（用于组合键）
void tmux_send_keys_no_enter(const std::vector<std::string> &pane_ids, const std::string &cmd) {
    if (pane_ids.empty()) return;
    std::string escaped_cmd = cmd;
    size_t pos = 0;
    while ((pos = escaped_cmd.find('\'', pos)) != std::string::npos) {
        escaped_cmd.replace(pos, 1, "'\\''");
        pos += 4;
    }
    for (const auto &pane_id : pane_ids) {
        std::string full_cmd = fmt::format("tmux send-keys -t '{}' '{}'", pane_id, escaped_cmd);
        std::system(full_cmd.c_str());
    }
}

// 发送 Ctrl+C 到指定窗格（中断正在运行的程序）
void tmux_send_ctrlc(const std::vector<std::string> &pane_ids) {
    if (pane_ids.empty()) return;
    for (const auto &pane_id : pane_ids) {
        std::string full_cmd = fmt::format("tmux send-keys -t '{}' C-c", pane_id);
        std::system(full_cmd.c_str());
    }
}

// 发送 Ctrl+Q 到指定窗格（用于退出 micro 等编辑器）
void tmux_send_ctrlq(const std::vector<std::string> &pane_ids) {
    if (pane_ids.empty()) return;
    for (const auto &pane_id : pane_ids) {
        std::string full_cmd = fmt::format("tmux send-keys -t '{}' C-q", pane_id);
        std::system(full_cmd.c_str());
    }
}

// 获取 tmux 窗格当前正在运行的命令名（只检查第一个窗格）
std::string tmux_pane_current_command(const std::vector<std::string> &pane_ids) {
    if (pane_ids.empty()) return "";
    // 只检查第一个窗格
    const auto &pane_id = pane_ids[0];
    std::string full_cmd = fmt::format("tmux display-message -p -t '{}' '#{{pane_current_command}}'", pane_id);
    FILE *fp = popen(full_cmd.c_str(), "r");
    if (!fp) return "";
    char buf[128] = {};
    std::string result;
    if (fgets(buf, sizeof(buf), fp) != nullptr) {
        result = buf;
        if (!result.empty() && result.back() == '\n') result.pop_back();
    }
    pclose(fp);
    return result;
}

// 检查单个 tmux 窗格是否正在运行程序（非 shell 提示符状态）
bool tmux_pane_is_busy(const std::string &pane_id) {
    if (pane_id.empty()) return false;
    std::string full_cmd = fmt::format("tmux display-message -p -t '{}' '#{{pane_current_command}}'", pane_id);
    FILE *fp = popen(full_cmd.c_str(), "r");
    if (!fp) return false;
    char buf[128] = {};
    std::string result;
    if (fgets(buf, sizeof(buf), fp) != nullptr) {
        result = buf;
        if (!result.empty() && result.back() == '\n') result.pop_back();
    }
    pclose(fp);
    if (result.empty()) return false;
    if (result != "zsh" && result != "bash" && result != "sh" && result != "fish" && result != "dash" && result != "ksh") {
        return true;
    }
    return false;
}

// 向多个 cd 窗格发送命令，跳过正在运行程序的窗格
// 返回跳过的窗格数量
int tmux_send_keys_cd(const std::vector<std::string> &pane_ids, const std::string &cmd) {
    if (pane_ids.empty()) return 0;
    int skipped = 0;
    std::string escaped_cmd = cmd;
    size_t pos = 0;
    while ((pos = escaped_cmd.find('\'', pos)) != std::string::npos) {
        escaped_cmd.replace(pos, 1, "'\\''");
        pos += 4;
    }
    for (const auto &pane_id : pane_ids) {
        if (tmux_pane_is_busy(pane_id)) {
            skipped++;
            continue;
        }
        std::string full_cmd = fmt::format("tmux send-keys -t '{}' '{}' Enter", pane_id, escaped_cmd);
        std::system(full_cmd.c_str());
    }
    return skipped;
}

// 智能中断：根据正在运行的程序选择合适的退出方式
void tmux_smart_interrupt(const std::vector<std::string> &pane_ids) {
    if (pane_ids.empty()) return;
    std::string cmd = tmux_pane_current_command(pane_ids);
    if (cmd.empty()) return;
    if (cmd == "zsh" || cmd == "bash" || cmd == "sh" || cmd == "fish" || cmd == "dash" || cmd == "ksh") {
        return;
    }
    if (cmd == "micro" || cmd == "vim" || cmd == "nvim" || cmd == "nano" || cmd == "vi" || cmd == "emacs") {
        tmux_send_ctrlq(pane_ids);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        tmux_send_ctrlc(pane_ids);
    } else {
        tmux_send_ctrlc(pane_ids);
    }
}

//============================================================================
// 处理空格键操作
//============================================================================

std::string handle_space_action(TreeNode &node, const std::vector<std::string> &pane_ids,
                                const std::vector<std::string> &cd_pane_ids) {
    if (node.is_dir) {
        // 向每个 cd 窗格发送 cd 命令，跳过正在运行程序的窗格
        if (!cd_pane_ids.empty()) {
            int skipped = tmux_send_keys_cd(cd_pane_ids, fmt::format("cd '{}'", node.path.string()));
            if (skipped > 0) {
                // 仍然要展开/折叠目录
                node.expanded = !node.expanded;
                if (node.expanded) {
                    return fmt::format("展开目录（{} 个 cd 窗格忙，跳过 cd）: {}", skipped, node.display_name);
                } else {
                    return fmt::format("折叠目录（{} 个 cd 窗格忙，跳过 cd）: {}", skipped, node.display_name);
                }
            }
            return fmt::format("在窗格中 cd 到目录: {}", node.display_name);
        }
        // 没有指定 cd 窗格，切换展开/折叠
        node.expanded = !node.expanded;
        if (node.expanded) {
            return fmt::format("展开目录: {}", node.display_name);
        } else {
            return fmt::format("折叠目录: {}", node.display_name);
        }
    } else if (node.is_sym) {
        return "符号链接，跳过";
    } else {
        // 文件操作
        std::string filepath = node.path.string();

        if (is_document_file(node.path)) {
            // 文档文件 - 用 micro 打开（始终中断正在运行的程序）
            if (!pane_ids.empty()) {
                tmux_smart_interrupt(pane_ids);
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                tmux_send_keys(pane_ids, fmt::format("micro '{}'", filepath));
                return fmt::format("在 tmux 窗格中用 micro 打开: {}", node.display_name);
            } else {
                return fmt::format("未指定 tmux 窗格，无法打开文件: {}", node.display_name);
            }
        } else if (is_archive_file(node.path)) {
            // 压缩包 - 显示内容（始终中断正在运行的程序）
            if (!pane_ids.empty()) {
                tmux_smart_interrupt(pane_ids);
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                tmux_send_keys(pane_ids, fmt::format("echo '=== {} 内容 ==='", node.display_name));
                auto ext = node.path.extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".zip") {
                    tmux_send_keys(pane_ids, fmt::format("unzip -l '{}' | head -50", filepath));
                } else if (ext == ".rar") {
                    tmux_send_keys(pane_ids, fmt::format("unrar l '{}' | head -50", filepath));
                } else if (ext == ".7z") {
                    tmux_send_keys(pane_ids, fmt::format("7z l '{}' | head -50", filepath));
                } else {
                    tmux_send_keys(pane_ids, fmt::format("tar tf '{}' | head -50", filepath));
                }
                return fmt::format("显示压缩包内容: {}", node.display_name);
            } else {
                return fmt::format("未指定 tmux 窗格，无法显示压缩包: {}", node.display_name);
            }
        } else if (node.is_exec) {
            // 可执行文件 - 直接执行（始终中断正在运行的程序）
            if (!pane_ids.empty()) {
                tmux_smart_interrupt(pane_ids);
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                tmux_send_keys(pane_ids, fmt::format("'{}'", filepath));
                return fmt::format("执行: {}", node.display_name);
            } else {
                return fmt::format("未指定 tmux 窗格，无法执行: {}", node.display_name);
            }
        } else {
            return fmt::format("未知文件类型，跳过: {}", node.display_name);
        }
    }
}

//============================================================================
// inotify 监控辅助（用于交互模式实时监听文件变动）
//============================================================================

struct WatchDir {
    int wd;
    fs::path path;
};

void add_watch_recursive(int inotify_fd, const fs::path &dir,
                         std::vector<WatchDir> &watches, bool show_hidden) {
    // 添加当前目录
    int wd = inotify_add_watch(inotify_fd, dir.c_str(),
                               IN_CREATE | IN_DELETE | IN_MODIFY |
                               IN_MOVED_FROM | IN_MOVED_TO |
                               IN_ATTRIB | IN_DELETE_SELF);
    if (wd >= 0) {
        watches.push_back({wd, dir});
    }

    // 递归添加子目录
    std::error_code ec;
    for (auto &entry : fs::directory_iterator(dir, ec)) {
        if (entry.is_directory()) {
            if (!show_hidden && is_hidden(entry.path())) continue;
            add_watch_recursive(inotify_fd, entry.path(), watches, show_hidden);
        }
    }
}

//============================================================================
// 交互模式主循环
//============================================================================

void interactive_mode(const fs::path &root_path, bool show_hidden,
                      const std::vector<std::string> &pane_ids,
                      const std::vector<std::string> &cd_pane_ids) {
    // 构建树节点列表
    std::vector<TreeNode> all_nodes;
    // 初始构建（仅根目录下一级）
    {
        std::vector<fs::directory_entry> entries;
        try {
            for (auto &entry : fs::directory_iterator(root_path)) {
                if (!show_hidden && is_hidden(entry.path())) continue;
                entries.push_back(entry);
            }
        } catch (const fs::filesystem_error &) {}

        std::sort(entries.begin(), entries.end(),
                  [](const fs::directory_entry &a, const fs::directory_entry &b) {
                      bool a_is_dir = a.is_directory();
                      bool b_is_dir = b.is_directory();
                      if (a_is_dir != b_is_dir) return a_is_dir > b_is_dir;
                      return a.path().filename().string() < b.path().filename().string();
                  });

        for (auto &entry : entries) {
            all_nodes.emplace_back(entry, 0);
        }
    }

    // 初始化 inotify 监控
    int inotify_fd = inotify_init1(IN_NONBLOCK);
    std::vector<WatchDir> watches;
    if (inotify_fd >= 0) {
        add_watch_recursive(inotify_fd, root_path, watches, show_hidden);
    }

    int cursor_pos = 0;
    std::string status_msg;
    bool need_render = true;

    // 设置终端为原始模式
    enable_raw_mode();

    // 隐藏光标
    fmt::print("\033[?25l");

    // 注册信号恢复终端
    signal(SIGINT, [](int) {
        disable_raw_mode();
        fmt::print("\033[?25h\033[2J\033[H");
        exit(0);
    });
    signal(SIGTERM, [](int) {
        disable_raw_mode();
        fmt::print("\033[?25h\033[2J\033[H");
        exit(0);
    });

    int scroll_offset = 0;

    // 事件缓冲区
    char inotify_buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    auto last_refresh = std::chrono::steady_clock::now();

    while (g_running) {
        // 使用 poll 同时监听 stdin 和 inotify
        struct pollfd pfds[2];
        pfds[0].fd = STDIN_FILENO;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        pfds[1].fd = inotify_fd;
        pfds[1].events = POLLIN;
        pfds[1].revents = 0;

        int poll_ret = poll(pfds, 2, 500); // 500ms 超时

        if (poll_ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // 检查 inotify 事件（文件变动）
        if (poll_ret > 0 && (pfds[1].revents & POLLIN)) {
            ssize_t len = read(inotify_fd, inotify_buf, sizeof(inotify_buf));
            if (len > 0) {
                bool need_refresh = false;
                bool dir_changed = false;

                char *ptr = inotify_buf;
                while (ptr < inotify_buf + len) {
                    auto *event = reinterpret_cast<struct inotify_event *>(ptr);
                    if (event->mask & (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO)) {
                        need_refresh = true;
                    }
                    if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
                        dir_changed = true;
                    }
                    if ((event->mask & IN_DELETE_SELF) || (event->mask & IN_MOVED_FROM)) {
                        need_refresh = true;
                        dir_changed = true;
                    }
                    ptr += sizeof(struct inotify_event) + event->len;
                }

                if (need_refresh) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refresh).count();
                    if (elapsed >= 200) {
                        last_refresh = now;

                        // 如果目录结构变了，重新添加监控
                        if (dir_changed) {
                            for (auto &w : watches) {
                                inotify_rm_watch(inotify_fd, w.wd);
                            }
                            watches.clear();
                            add_watch_recursive(inotify_fd, root_path, watches, show_hidden);
                        }

                        // 保存当前展开状态
                        std::unordered_map<std::string, bool> expanded_state;
                        for (const auto &node : all_nodes) {
                            if (node.is_dir) {
                                expanded_state[node.path.string()] = node.expanded;
                            }
                        }

                        // 重建树节点列表
                        all_nodes.clear();
                        std::vector<fs::directory_entry> entries;
                        try {
                            for (auto &entry : fs::directory_iterator(root_path)) {
                                if (!show_hidden && is_hidden(entry.path())) continue;
                                entries.push_back(entry);
                            }
                        } catch (const fs::filesystem_error &) {}

                        std::sort(entries.begin(), entries.end(),
                                  [](const fs::directory_entry &a, const fs::directory_entry &b) {
                                      bool a_is_dir = a.is_directory();
                                      bool b_is_dir = b.is_directory();
                                      if (a_is_dir != b_is_dir) return a_is_dir > b_is_dir;
                                      return a.path().filename().string() < b.path().filename().string();
                                  });

                        for (auto &entry : entries) {
                            all_nodes.emplace_back(entry, 0);
                        }

                        // 恢复展开状态
                        for (auto &node : all_nodes) {
                            if (node.is_dir) {
                                auto it = expanded_state.find(node.path.string());
                                if (it != expanded_state.end() && it->second) {
                                    // 重新展开该目录
                                    node.expanded = true;
                                    std::vector<fs::directory_entry> child_entries;
                                    try {
                                        for (auto &child : fs::directory_iterator(node.path)) {
                                            if (!show_hidden && is_hidden(child.path())) continue;
                                            child_entries.push_back(child);
                                        }
                                    } catch (const fs::filesystem_error &) {}
                                    std::sort(child_entries.begin(), child_entries.end(),
                                              [](const fs::directory_entry &a, const fs::directory_entry &b) {
                                                  bool a_is_dir = a.is_directory();
                                                  bool b_is_dir = b.is_directory();
                                                  if (a_is_dir != b_is_dir) return a_is_dir > b_is_dir;
                                                  return a.path().filename().string() < b.path().filename().string();
                                              });
                                    // 找到这个节点在 all_nodes 中的位置
                                    auto node_it = std::find_if(all_nodes.begin(), all_nodes.end(),
                                        [&node](const TreeNode &n) { return n.path == node.path; });
                                    if (node_it != all_nodes.end()) {
                                        int pos = std::distance(all_nodes.begin(), node_it);
                                        std::vector<TreeNode> children;
                                        for (auto &child : child_entries) {
                                            children.emplace_back(child, node.depth + 1);
                                        }
                                        all_nodes.insert(all_nodes.begin() + pos + 1,
                                                         children.begin(), children.end());
                                    }
                                }
                            }
                        }

                        // 修正光标位置
                        if (cursor_pos >= (int)all_nodes.size()) {
                            cursor_pos = (int)all_nodes.size() - 1;
                        }
                        if (cursor_pos < 0) cursor_pos = 0;

                        status_msg = "🔄 文件变动已刷新";
                        need_render = true;
                    }
                }
            }
        }

        // 检查键盘输入
        if (poll_ret > 0 && (pfds[0].revents & POLLIN)) {
            if (need_render) {
                render_interactive(all_nodes, cursor_pos, root_path, pane_ids, cd_pane_ids, status_msg, scroll_offset);
                need_render = false;
                status_msg.clear();
            }

            Key key = read_key();

            switch (key) {
                case Key::UP:
                    if (cursor_pos > 0) {
                        cursor_pos--;
                        need_render = true;
                    }
                    break;

                case Key::DOWN:
                    if (cursor_pos < (int)all_nodes.size() - 1) {
                        cursor_pos++;
                        need_render = true;
                    }
                    break;

                case Key::SPACE: {
                    if (cursor_pos >= 0 && cursor_pos < (int)all_nodes.size()) {
                        auto &node = all_nodes[cursor_pos];

                        if (node.is_dir) {
                            // 向每个 cd 窗格发送 cd 命令，跳过正在运行程序的窗格
                            int skipped_cd = 0;
                            if (!cd_pane_ids.empty()) {
                                skipped_cd = tmux_send_keys_cd(cd_pane_ids, fmt::format("cd '{}'", node.path.string()));
                            }

                            // 无论是否指定 cd 窗格，文件树都要展开/折叠
                            bool was_expanded = node.expanded;
                            node.expanded = !was_expanded;

                            if (node.expanded) {
                                // 展开：插入子节点
                                std::vector<fs::directory_entry> entries;
                                try {
                                    for (auto &entry : fs::directory_iterator(node.path)) {
                                        if (!show_hidden && is_hidden(entry.path())) continue;
                                        entries.push_back(entry);
                                    }
                                } catch (const fs::filesystem_error &) {}

                                std::sort(entries.begin(), entries.end(),
                                          [](const fs::directory_entry &a, const fs::directory_entry &b) {
                                              bool a_is_dir = a.is_directory();
                                              bool b_is_dir = b.is_directory();
                                              if (a_is_dir != b_is_dir) return a_is_dir > b_is_dir;
                                              return a.path().filename().string() < b.path().filename().string();
                                          });

                                std::vector<TreeNode> children;
                                for (auto &entry : entries) {
                                    children.emplace_back(entry, node.depth + 1);
                                }

                                // 在当前节点后面插入子节点
                                all_nodes.insert(all_nodes.begin() + cursor_pos + 1,
                                                 children.begin(), children.end());

                                if (!cd_pane_ids.empty()) {
                                    if (skipped_cd > 0 && skipped_cd == (int)cd_pane_ids.size()) {
                                        status_msg = fmt::format("展开（所有 cd 窗格忙，跳过 cd）: {}", node.display_name);
                                    } else if (skipped_cd > 0) {
                                        status_msg = fmt::format("展开 + cd 到目录（{} 个窗格忙，跳过）: {}", skipped_cd, node.display_name);
                                    } else {
                                        status_msg = fmt::format("展开 + cd 到目录: {}", node.display_name);
                                    }
                                } else {
                                    status_msg = fmt::format("展开目录: {}", node.display_name);
                                }
                            } else {
                                // 折叠：移除子节点
                                int remove_count = 0;
                                int start_remove = cursor_pos + 1;
                                for (int i = start_remove; i < (int)all_nodes.size(); ++i) {
                                    if (all_nodes[i].depth <= node.depth) break;
                                    remove_count++;
                                }
                                if (remove_count > 0) {
                                    all_nodes.erase(all_nodes.begin() + start_remove,
                                                    all_nodes.begin() + start_remove + remove_count);
                                }
                                if (!cd_pane_ids.empty()) {
                                    if (skipped_cd > 0 && skipped_cd == (int)cd_pane_ids.size()) {
                                        status_msg = fmt::format("折叠（所有 cd 窗格忙，跳过 cd）: {}", node.display_name);
                                    } else if (skipped_cd > 0) {
                                        status_msg = fmt::format("折叠 + cd 到目录（{} 个窗格忙，跳过）: {}", skipped_cd, node.display_name);
                                    } else {
                                        status_msg = fmt::format("折叠 + cd 到目录: {}", node.display_name);
                                    }
                                } else {
                                    status_msg = fmt::format("折叠目录: {}", node.display_name);
                                }
                            }
                            need_render = true;
                        } else {
                            // 文件操作
                            status_msg = handle_space_action(node, pane_ids, cd_pane_ids);
                            need_render = true;
                        }
                    }
                    break;
                }

                case Key::MKDIR: {
                    // 在光标所在目录下创建新目录
                    fs::path target_dir;
                    if (cursor_pos >= 0 && cursor_pos < (int)all_nodes.size()) {
                        auto &node = all_nodes[cursor_pos];
                        if (node.is_dir) {
                            target_dir = node.path;
                        } else {
                            target_dir = node.path.parent_path();
                        }
                    } else {
                        target_dir = root_path;
                    }

                    status_msg = fmt::format("📁 在 {} 下创建目录，请输入目录名: ", target_dir.filename().string());
                    need_render = true;
                    render_interactive(all_nodes, cursor_pos, root_path, pane_ids, cd_pane_ids, status_msg, scroll_offset);
                    need_render = false;

                    fmt::print(style::info_color, "  📁 输入目录名: ");
                    fflush(stdout);
                    std::string dirname = read_line_input();

                    if (!dirname.empty()) {
                        fs::path new_dir = target_dir / dirname;
                        std::error_code ec;
                        if (fs::create_directory(new_dir, ec)) {
                            status_msg = fmt::format("✅ 已创建目录: {}", dirname);
                        } else {
                            status_msg = fmt::format("❌ 创建失败: {} ({})", dirname, ec.message());
                        }
                    } else {
                        status_msg = "❌ 目录名不能为空";
                    }
                    need_render = true;
                    break;
                }

                case Key::NEWFILE: {
                    // 在光标所在目录下用 micro 创建新文件
                    fs::path target_dir;
                    if (cursor_pos >= 0 && cursor_pos < (int)all_nodes.size()) {
                        auto &node = all_nodes[cursor_pos];
                        if (node.is_dir) {
                            target_dir = node.path;
                        } else {
                            target_dir = node.path.parent_path();
                        }
                    } else {
                        target_dir = root_path;
                    }

                    status_msg = fmt::format("📄 在 {} 下创建文件，请输入文件名: ", target_dir.filename().string());
                    need_render = true;
                    render_interactive(all_nodes, cursor_pos, root_path, pane_ids, cd_pane_ids, status_msg, scroll_offset);
                    need_render = false;

                    fmt::print(style::info_color, "  📄 输入文件名: ");
                    fflush(stdout);
                    std::string filename = read_line_input();

                    if (!filename.empty()) {
                        fs::path new_file = target_dir / filename;
                        if (!pane_ids.empty()) {
                            tmux_smart_interrupt(pane_ids);
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            tmux_send_keys(pane_ids, fmt::format("micro '{}'", new_file.string()));
                            status_msg = fmt::format("✅ 在窗格中用 micro 创建: {}", filename);
                        } else {
                            std::error_code ec;
                            if (fs::exists(new_file)) {
                                status_msg = fmt::format("⚠️ 文件已存在: {}", filename);
                            } else {
                                std::ofstream ofs(new_file.string());
                                ofs.close();
                                status_msg = fmt::format("✅ 已创建空文件: {}", filename);
                            }
                        }
                    } else {
                        status_msg = "❌ 文件名不能为空";
                    }
                    need_render = true;
                    break;
                }

                case Key::DELETE: {
                    // 递归删除光标指向的目录或文件
                    if (cursor_pos >= 0 && cursor_pos < (int)all_nodes.size()) {
                        auto &node = all_nodes[cursor_pos];
                        std::string target_name = node.display_name;
                        fs::path target_path = node.path;

                        status_msg = fmt::format("⚠️ 确认删除 \"{}\"？(y/n): ", target_name);
                        need_render = true;
                        render_interactive(all_nodes, cursor_pos, root_path, pane_ids, cd_pane_ids, status_msg, scroll_offset);
                        need_render = false;

                        fmt::print(style::error_color, "  ⚠️ 确认删除 \"{}\"？(y/n): ", target_name);
                        fflush(stdout);

                        // 读取确认（临时切换到阻塞模式，等待用户输入）
                        char confirm = 0;
                        // 从当前原始模式临时改为 VMIN=1, VTIME=0（阻塞读一个字符）
                        struct termios raw;
                        tcgetattr(STDIN_FILENO, &raw);
                        raw.c_cc[VMIN] = 1;
                        raw.c_cc[VTIME] = 0;
                        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

                        if (read(STDIN_FILENO, &confirm, 1) == 1) {
                            if (confirm == 'y' || confirm == 'Y') {
                                std::error_code ec;
                                uintmax_t removed = fs::remove_all(target_path, ec);
                                if (!ec) {
                                    // 从 all_nodes 中移除被删除的节点及其子节点
                                    int remove_start = cursor_pos;
                                    int remove_count = 0;
                                    int base_depth = all_nodes[cursor_pos].depth;
                                    for (int i = remove_start; i < (int)all_nodes.size(); ++i) {
                                        if (all_nodes[i].depth < base_depth) break;
                                        if (all_nodes[i].depth == base_depth && i > remove_start) break;
                                        remove_count++;
                                    }
                                    all_nodes.erase(all_nodes.begin() + remove_start,
                                                    all_nodes.begin() + remove_start + remove_count);
                                    // 修正光标位置
                                    if (cursor_pos >= (int)all_nodes.size()) {
                                        cursor_pos = (int)all_nodes.size() - 1;
                                    }
                                    if (cursor_pos < 0) cursor_pos = 0;
                                    status_msg = fmt::format("✅ 已删除: {} ({} 项)", target_name, removed);
                                } else {
                                    status_msg = fmt::format("❌ 删除失败: {} ({})", target_name, ec.message());
                                }
                            } else {
                                status_msg = fmt::format("已取消删除: {}", target_name);
                            }
                        }

                        // 恢复原始终端模式（VMIN=0, VTIME=1 非阻塞）
                        {
                            struct termios restore;
                            tcgetattr(STDIN_FILENO, &restore);
                            restore.c_cc[VMIN] = 0;
                            restore.c_cc[VTIME] = 1;
                            tcsetattr(STDIN_FILENO, TCSAFLUSH, &restore);
                        }
                    } else {
                        status_msg = "❌ 没有选中任何项目";
                    }
                    need_render = true;
                    break;
                }

                case Key::CDROOT: {
                    // cd 到最上层目录（根目录）
                    if (!cd_pane_ids.empty()) {
                        int skipped = tmux_send_keys_cd(cd_pane_ids, fmt::format("cd '{}'", root_path.string()));
                        if (skipped > 0 && skipped == (int)cd_pane_ids.size()) {
                            status_msg = "⚠️ 所有 cd 窗格都在运行程序，跳过";
                        } else if (skipped > 0) {
                            status_msg = fmt::format("📂 已 cd 到根目录（{} 个窗格忙，跳过）: {}", skipped, root_path.filename().string());
                        } else {
                            status_msg = fmt::format("📂 已 cd 到根目录: {}", root_path.filename().string());
                        }
                    } else {
                        status_msg = "❌ 未指定 cd 窗格 (-c)";
                    }
                    need_render = true;
                    break;
                }

                case Key::Q:
                case Key::ESC:
                    g_running = false;
                    break;

                default:
                    break;
            }
        } else if (poll_ret == 0) {
            // 超时：没有按键也没有文件变动，但如果有待渲染的内容则渲染
            if (need_render) {
                render_interactive(all_nodes, cursor_pos, root_path, pane_ids, cd_pane_ids, status_msg, scroll_offset);
                need_render = false;
                status_msg.clear();
            }
        }
    }

    // 清理 inotify
    if (inotify_fd >= 0) {
        for (auto &w : watches) {
            inotify_rm_watch(inotify_fd, w.wd);
        }
        close(inotify_fd);
    }

    // 恢复终端
    disable_raw_mode();
    fmt::print("\033[?25h\033[2J\033[H");
    fmt::print(style::watch_color, "  👋 交互模式已退出\n");
}
//============================================================================
// 树形打印核心逻辑（非交互模式 - 保留原有功能）
//============================================================================

struct TreeStats {
    int dirs = 0;
    int files = 0;
    int symlinks = 0;
    int hidden = 0;
    uintmax_t total_size = 0;
};

void print_tree(const fs::path &dir_path, const std::string &prefix, bool recursive,
                TreeStats &stats, bool show_hidden) {
    std::vector<fs::directory_entry> entries;

    try {
        for (auto &entry : fs::directory_iterator(dir_path)) {
            if (!show_hidden && is_hidden(entry.path())) continue;
            entries.push_back(entry);
        }
    } catch (const fs::filesystem_error &e) {
        fmt::print(stderr, style::error_color, "❌ 错误: {}\n", e.what());
        return;
    }

    std::sort(entries.begin(), entries.end(),
              [](const fs::directory_entry &a, const fs::directory_entry &b) {
                  bool a_is_dir = a.is_directory();
                  bool b_is_dir = b.is_directory();
                  if (a_is_dir != b_is_dir) return a_is_dir > b_is_dir;
                  return a.path().filename().string() < b.path().filename().string();
              });

    for (size_t i = 0; i < entries.size(); ++i) {
        auto &entry = entries[i];
        bool is_last = (i == entries.size() - 1);

        std::string connector = is_last ? "└── " : "├── ";
        std::string child_prefix = prefix + (is_last ? "    " : "│   ");

        bool is_dir = entry.is_directory();
        bool is_sym = entry.is_symlink();
        bool is_hid = is_hidden(entry.path());
        bool is_exec = is_executable(entry);

        if (is_dir) ++stats.dirs;
        else if (is_sym) ++stats.symlinks;
        else ++stats.files;
        if (is_hid) ++stats.hidden;

        uintmax_t fsize = 0;
        std::string size_str;
        if (entry.is_regular_file()) {
            std::error_code ec;
            fsize = entry.file_size(ec);
            if (!ec) {
                stats.total_size += fsize;
                size_str = format_size(fsize);
            }
        }

        std::string time_str;
        try {
            auto ftime = entry.last_write_time();
            time_str = format_time(ftime);
        } catch (...) {}

        std::string filename = entry.path().filename().string();
        std::string icon = std::string(file_icon(entry));

        fmt::text_style name_color;
        if (is_dir)            name_color = style::dir_color;
        else if (is_sym)       name_color = style::link_color;
        else if (is_exec)      name_color = style::exec_color;
        else if (is_hid)       name_color = style::hidden_color;
        else                   name_color = style::file_color;

        fmt::print(style::line_color, "{}{}", prefix, connector);
        fmt::print("{} ", icon);
        fmt::print(name_color, "{}", filename);

        if (is_sym) {
            std::error_code ec;
            auto target = fs::read_symlink(entry.path(), ec);
            if (!ec) {
                fmt::print(style::line_color, " → ");
                fmt::print(style::link_color, "{}", target.string());
            }
        }

        if (!size_str.empty() || !time_str.empty()) {
            fmt::print(style::line_color, " ·");
            if (!size_str.empty()) fmt::print(style::size_color, " {}", size_str);
            if (!time_str.empty()) fmt::print(style::date_color, " {}", time_str);
        }

        fmt::print("\n");

        if (recursive && is_dir) {
            print_tree(entry.path(), child_prefix, recursive, stats, show_hidden);
        }
    }
}

//============================================================================
// 打印完整目录树（带标题和统计）
//============================================================================

void print_full_tree(const fs::path &root_path, bool recursive, bool show_hidden,
                     const std::string &change_msg = "") {
    // 清屏并回到顶部
    fmt::print("\033[2J\033[H");

    // 标题
    fmt::print(style::title_color, "  📂 目录树: ");
    fmt::print(fg(fmt::color::white) | fmt::emphasis::bold, "{}", root_path.string());

    // 如果有变动消息，显示
    if (!change_msg.empty()) {
        fmt::print(style::change_color, "  [{}]", change_msg);
    }
    fmt::print("\n");

    fmt::print(style::line_color, "  ");
    for (size_t i = 0; i < root_path.string().length() + 12; ++i) fmt::print("─");
    fmt::print("\n\n");

    fmt::print(style::dir_color, "  📁 .\n");

    TreeStats stats;
    print_tree(root_path, "  ", recursive, stats, show_hidden);

    // 统计
    fmt::print("\n");
    fmt::print(style::stats_color, "  ── 统计 ──\n");
    fmt::print(style::stats_color, "  📁 目录: {}\n", stats.dirs);
    fmt::print(style::stats_color, "  📄 文件: {}\n", stats.files);
    if (stats.symlinks > 0) fmt::print(style::stats_color, "  🔗 链接: {}\n", stats.symlinks);
    if (stats.hidden > 0)   fmt::print(style::stats_color, "  🙈 隐藏: {}\n", stats.hidden);
    fmt::print(style::stats_color, "  💾 总计: {}\n", format_size(stats.total_size));
    fmt::print("\n");
}

//============================================================================
// inotify 监控循环（非交互模式使用）
//============================================================================

void watch_loop(const fs::path &root_path, bool recursive, bool show_hidden) {
    int inotify_fd = inotify_init1(IN_NONBLOCK);
    if (inotify_fd < 0) {
        fmt::print(stderr, style::error_color, "❌ 无法初始化 inotify\n");
        return;
    }

    std::vector<WatchDir> watches;
    add_watch_recursive(inotify_fd, root_path, watches, show_hidden);

    // 事件缓冲区
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));

    // 防抖：记录上次刷新时间
    auto last_refresh = std::chrono::steady_clock::now();

    while (g_running) {
        struct pollfd pfd = {inotify_fd, POLLIN, 0};
        int ret = poll(&pfd, 1, 500); // 500ms 超时

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (ret == 0) continue; // 超时，继续循环

        // 读取事件
        ssize_t len = read(inotify_fd, buf, sizeof(buf));
        if (len <= 0) continue;

        // 检查是否有需要刷新的变动
        bool need_refresh = false;
        bool dir_changed = false;
        std::string change_info;

        char *ptr = buf;
        while (ptr < buf + len) {
            auto *event = reinterpret_cast<struct inotify_event *>(ptr);

            if (event->mask & (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO)) {
                need_refresh = true;
                if (event->len > 0) {
                    if (event->mask & IN_CREATE)   change_info = "+ " + std::string(event->name);
                    if (event->mask & IN_DELETE)   change_info = "- " + std::string(event->name);
                    if (event->mask & IN_MOVED_FROM) change_info = "⇢ " + std::string(event->name);
                    if (event->mask & IN_MOVED_TO)   change_info = "⇠ " + std::string(event->name);
                }
            }

            // 如果是新创建了目录，需要添加监控
            if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
                dir_changed = true;
            }

            // 如果删除了目录，移除监控（自动的，但标记刷新）
            if ((event->mask & IN_DELETE_SELF) || (event->mask & IN_MOVED_FROM)) {
                need_refresh = true;
                dir_changed = true;
            }

            ptr += sizeof(struct inotify_event) + event->len;
        }

        if (need_refresh) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refresh).count();

            // 防抖：至少间隔 200ms
            if (elapsed >= 200) {
                last_refresh = now;

                // 如果目录结构变了，重新添加监控
                if (dir_changed) {
                    // 移除所有旧监控
                    for (auto &w : watches) {
                        inotify_rm_watch(inotify_fd, w.wd);
                    }
                    watches.clear();
                    add_watch_recursive(inotify_fd, root_path, watches, show_hidden);
                }

                print_full_tree(root_path, recursive, show_hidden, change_info);
            }
        }
    }

    // 清理
    for (auto &w : watches) {
        inotify_rm_watch(inotify_fd, w.wd);
    }
    close(inotify_fd);
}

//============================================================================
// 主函数
//============================================================================

int main(int argc, char *argv[]) {
    fs::path root_path = fs::current_path();
    bool recursive = false;
    bool show_hidden = false;
    bool watch_mode = false;
    bool interactive = false;
    std::string pane_id_str;
    std::string cd_pane_id_str;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-r" || arg == "--recursive") {
            recursive = true;
        } else if (arg == "-a" || arg == "--all") {
            show_hidden = true;
        } else if (arg == "-w" || arg == "--watch") {
            watch_mode = true;
        } else if (arg == "-i" || arg == "--interactive") {
            interactive = true;
        } else if (arg == "-p" || arg == "--pane") {
            if (i + 1 < argc) {
                pane_id_str = argv[++i];
            } else {
                fmt::print(stderr, style::error_color, "❌ 错误: -p/--pane 需要指定 tmux 窗格 ID\n");
                return 1;
            }
        } else if (arg == "-c" || arg == "--cd-pane") {
            if (i + 1 < argc) {
                cd_pane_id_str = argv[++i];
            } else {
                fmt::print(stderr, style::error_color, "❌ 错误: -c/--cd-pane 需要指定 tmux 窗格 ID\n");
                return 1;
            }
        } else if (arg == "-h" || arg == "--help") {
            fmt::print(style::title_color, "listtree v2.0.0 - 实时目录树查看器\n");
            fmt::print("在终端中美观地显示目录树结构，支持实时监控文件变动\n\n");
            fmt::print(style::title_color, "用法:\n");
            fmt::print("  listtree [选项...] [目录路径]\n\n");
            fmt::print(style::title_color, "选项:\n");
            fmt::print("  -r, --recursive    递归显示子目录\n");
            fmt::print("  -a, --all          显示隐藏文件\n");
            fmt::print("  -w, --watch        实时监控模式，文件变动自动刷新\n");
            fmt::print("  -i, --interactive  交互模式，支持方向键导航和空格操作\n");
            fmt::print("  -p, --pane ID      指定 tmux 窗格 ID（多个用逗号分隔，用于打开文件/执行）\n");
            fmt::print("  -c, --cd-pane ID   指定 tmux 窗格 ID（多个用逗号分隔，用于 cd 到目录）\n");
            fmt::print("  -h, --help         显示此帮助信息\n\n");
            fmt::print(style::title_color, "交互模式操作:\n");
            fmt::print("  ↑/↓        移动光标\n");
            fmt::print("  Space      目录:展开/折叠（若指定 -c 则同时在 cd 窗格中 cd 到该目录）\n");
            fmt::print("             文件:在 tmux 窗格中打开（若窗格正在运行程序则先中断）\n");
            fmt::print("  m [空格]   在光标所在目录下创建新目录（输入目录名后按 Enter）\n");
            fmt::print("  n [空格]   在光标所在目录下用 micro 创建新文件（输入文件名后按 Enter）\n");
            fmt::print("  d          递归删除光标指向的目录或文件（需确认 y/n）\n");
            fmt::print("  u          在 cd 窗格中 cd 到最上层目录（根目录）\n");
            fmt::print("  q/ESC      退出交互模式\n");
            fmt::print(style::watch_color, "  💡 交互模式自动监听文件变动，有变化时自动刷新\n\n");
            fmt::print(style::title_color, "示例:\n");
            fmt::print("  listtree\n");
            fmt::print("  listtree -r ~/code\n");
            fmt::print("  listtree -r -a .\n");
            fmt::print("  listtree -w -r .          # 实时监控当前目录\n");
            fmt::print("  listtree -w -r -a /path   # 实时监控（含隐藏文件）\n");
            fmt::print("  listtree -i               # 交互模式\n");
            fmt::print("  listtree -i -p 0          # 交互模式，tmux 窗格 0\n");
            fmt::print("  listtree -i -p 0,1        # 交互模式，tmux 窗格 0 和 1\n");
            fmt::print("  listtree -i -p mypane:1   # 交互模式，指定 tmux 窗格\n");
            fmt::print("  listtree -i -p 0 -c 1     # 交互模式，窗格0打开文件，窗格1 cd 到目录\n");
            fmt::print("  listtree -i -p 0,1 -c 2   # 交互模式，窗格0,1打开文件，窗格2 cd\n\n");
            fmt::print(style::watch_color, "💡 提示: 按 Ctrl+C 退出监控模式\n");
            return 0;
        } else {
            root_path = fs::path(arg);
        }
    }

    std::error_code ec;
    if (!fs::exists(root_path, ec)) {
        fmt::print(stderr, style::error_color, "❌ 错误: 路径不存在: {}\n", root_path.string());
        return 1;
    }
    if (!fs::is_directory(root_path, ec)) {
        fmt::print(stderr, style::error_color, "❌ 错误: 路径不是目录: {}\n", root_path.string());
        return 1;
    }

    root_path = fs::absolute(root_path);

    // 解析窗格 ID（支持逗号分隔的多个窗格）
    std::vector<std::string> pane_ids = parse_pane_ids(pane_id_str);
    std::vector<std::string> cd_pane_ids = parse_pane_ids(cd_pane_id_str);

    if (interactive) {
        // 交互模式
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        interactive_mode(root_path, show_hidden, pane_ids, cd_pane_ids);
    } else if (watch_mode) {
        // 实时监控模式
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        // 先打印一次
        print_full_tree(root_path, recursive, show_hidden);

        // 显示提示
        fmt::print(style::watch_color, "  👀 监控中... 按 Ctrl+C 退出\n");

        // 进入监控循环
        watch_loop(root_path, recursive, show_hidden);

        // 退出时清掉最后一行提示
        fmt::print("\033[2K\r");
        fmt::print(style::watch_color, "  👋 监控已退出\n");
    } else {
        // 一次性模式
        print_full_tree(root_path, recursive, show_hidden);
    }

    return 0;
}