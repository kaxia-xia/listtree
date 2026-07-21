//============================================================================
// listtree - 在终端中美观地显示目录树结构（支持实时监控）
// 用法: listtree [目录路径] [-r|--recursive] [-a|--all] [-w|--watch]
//============================================================================

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

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

//============================================================================
// 树形打印核心逻辑
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
// inotify 监控 - 递归添加所有子目录
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
// 监控循环
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

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-r" || arg == "--recursive") {
            recursive = true;
        } else if (arg == "-a" || arg == "--all") {
            show_hidden = true;
        } else if (arg == "-w" || arg == "--watch") {
            watch_mode = true;
        } else if (arg == "-h" || arg == "--help") {
            fmt::print(style::title_color, "listtree v2.0.0 - 实时目录树查看器\n");
            fmt::print("在终端中美观地显示目录树结构，支持实时监控文件变动\n\n");
            fmt::print(style::title_color, "用法:\n");
            fmt::print("  listtree [选项...] [目录路径]\n\n");
            fmt::print(style::title_color, "选项:\n");
            fmt::print("  -r, --recursive    递归显示子目录\n");
            fmt::print("  -a, --all          显示隐藏文件\n");
            fmt::print("  -w, --watch        实时监控模式，文件变动自动刷新\n");
            fmt::print("  -h, --help         显示此帮助信息\n\n");
            fmt::print(style::title_color, "示例:\n");
            fmt::print("  listtree\n");
            fmt::print("  listtree -r ~/code\n");
            fmt::print("  listtree -r -a .\n");
            fmt::print("  listtree -w -r .          # 实时监控当前目录\n");
            fmt::print("  listtree -w -r -a /path   # 实时监控（含隐藏文件）\n\n");
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

    if (watch_mode) {
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
