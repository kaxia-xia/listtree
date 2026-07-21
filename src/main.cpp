//============================================================================
// listtree - 在终端中美观地显示目录树结构
// 用法: listtree [目录路径] [-r|--recursive] [-a|--all]
//============================================================================

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/core.h>
#include <fmt/color.h>

namespace fs = std::filesystem;

//============================================================================
// 颜色与样式定义
//============================================================================
namespace style {

// 目录名颜色 - 亮蓝色加粗
inline constexpr auto dir_color = fmt::fg(fmt::color::deep_sky_blue) | fmt::emphasis::bold;

// 普通文件颜色 - 默认
inline constexpr auto file_color = fmt::fg(fmt::color::light_gray);

// 可执行文件颜色 - 亮绿色加粗
inline constexpr auto exec_color = fmt::fg(fmt::color::lime_green) | fmt::emphasis::bold;

// 符号链接颜色 - 亮青色斜体
inline constexpr auto link_color = fmt::fg(fmt::color::cyan) | fmt::emphasis::italic;

// 隐藏文件颜色 - 暗色斜体
inline constexpr auto hidden_color = fmt::fg(fmt::color::dim_gray) | fmt::emphasis::italic;

// 文件大小颜色
inline constexpr auto size_color = fmt::fg(fmt::color::gold);

// 日期颜色
inline constexpr auto date_color = fmt::fg(fmt::color::wheat);

// 统计信息颜色
inline constexpr auto stats_color = fmt::fg(fmt::color::yellow) | fmt::emphasis::bold;

// 错误信息颜色
inline constexpr auto error_color = fmt::fg(fmt::color::red) | fmt::emphasis::bold;

// 树形线条颜色
inline constexpr auto line_color = fmt::fg(fmt::color::steel_blue);

// 标题颜色
inline constexpr auto title_color = fmt::fg(fmt::color::hot_pink) | fmt::emphasis::bold;

} // namespace style

//============================================================================
// 工具函数
//============================================================================

// 判断文件是否可执行
bool is_executable(const fs::directory_entry &entry) {
    if (!entry.is_regular_file()) return false;
    auto perms = entry.status().permissions();
    return (perms & fs::perms::owner_exec) != fs::perms::none ||
           (perms & fs::perms::group_exec) != fs::perms::none ||
           (perms & fs::perms::others_exec) != fs::perms::none;
}

// 判断是否为隐藏文件/目录
bool is_hidden(const fs::path &p) {
    auto filename = p.filename().string();
    return !filename.empty() && filename[0] == '.';
}

// 格式化文件大小
std::string format_size(uintmax_t bytes) {
    constexpr const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unit_idx < 4) {
        size /= 1024.0;
        ++unit_idx;
    }

    if (unit_idx == 0) {
        return fmt::format("{} {}", bytes, units[unit_idx]);
    }
    return fmt::format("{:.1f} {}", size, units[unit_idx]);
}

// 获取文件修改时间的简短表示
std::string format_time(const fs::file_time_type &ftime) {
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    auto tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm *tm = std::localtime(&tt);

    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
    return std::string(buf);
}

// 获取文件类型图标
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

// 递归打印目录树
void print_tree(const fs::path &dir_path, const std::string &prefix, bool recursive,
                TreeStats &stats, bool show_hidden) {
    std::vector<fs::directory_entry> entries;

    try {
        for (auto &entry : fs::directory_iterator(dir_path)) {
            // 过滤隐藏文件
            if (!show_hidden && is_hidden(entry.path())) continue;
            entries.push_back(entry);
        }
    } catch (const fs::filesystem_error &e) {
        fmt::print(stderr, style::error_color, "❌ 错误: {}\n", e.what());
        return;
    }

    // 排序：目录在前，文件在后，各自按名称排序
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

        // 树形连接线
        std::string connector = is_last ? "└── " : "├── ";
        std::string child_prefix = prefix + (is_last ? "    " : "│   ");

        // 判断文件类型
        bool is_dir = entry.is_directory();
        bool is_sym = entry.is_symlink();
        bool is_hid = is_hidden(entry.path());
        bool is_exec = is_executable(entry);

        // 统计
        if (is_dir) ++stats.dirs;
        else if (is_sym) ++stats.symlinks;
        else ++stats.files;
        if (is_hid) ++stats.hidden;

        // 获取文件大小
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

        // 获取修改时间
        std::string time_str;
        try {
            auto ftime = entry.last_write_time();
            time_str = format_time(ftime);
        } catch (...) {
            // 忽略时间获取失败
        }

        // 文件名
        std::string filename = entry.path().filename().string();
        std::string icon = std::string(file_icon(entry));

        // 根据类型选择颜色
        fmt::text_style name_color;
        if (is_dir) {
            name_color = style::dir_color;
        } else if (is_sym) {
            name_color = style::link_color;
        } else if (is_exec) {
            name_color = style::exec_color;
        } else if (is_hid) {
            name_color = style::hidden_color;
        } else {
            name_color = style::file_color;
        }

        // 构建输出行
        // 树形线条
        fmt::print(style::line_color, "{}{}", prefix, connector);

        // 图标
        fmt::print("{} ", icon);

        // 文件名
        fmt::print(name_color, "{}", filename);

        // 符号链接目标
        if (is_sym) {
            std::error_code ec;
            auto target = fs::read_symlink(entry.path(), ec);
            if (!ec) {
                fmt::print(style::line_color, " → ");
                fmt::print(style::link_color, "{}", target.string());
            }
        }

        // 大小和时间信息
        if (!size_str.empty() || !time_str.empty()) {
            fmt::print(style::line_color, " ·");
            if (!size_str.empty()) {
                fmt::print(style::size_color, " {}", size_str);
            }
            if (!time_str.empty()) {
                fmt::print(style::date_color, " {}", time_str);
            }
        }

        fmt::print("\n");

        // 递归子目录
        if (recursive && is_dir) {
            print_tree(entry.path(), child_prefix, recursive, stats, show_hidden);
        }
    }
}

//============================================================================
// 主函数
//============================================================================

int main(int argc, char *argv[]) {
    // 解析命令行参数
    fs::path root_path = fs::current_path();
    bool recursive = false;
    bool show_hidden = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-r" || arg == "--recursive") {
            recursive = true;
        } else if (arg == "-a" || arg == "--all") {
            show_hidden = true;
        } else if (arg == "-h" || arg == "--help") {
            fmt::print(style::title_color, "listtree v1.0.0\n");
            fmt::print("在终端中美观地显示目录树结构\n\n");
            fmt::print(style::title_color, "用法:\n");
            fmt::print("  listtree [选项...] [目录路径]\n\n");
            fmt::print(style::title_color, "选项:\n");
            fmt::print("  -r, --recursive    递归显示子目录\n");
            fmt::print("  -a, --all          显示隐藏文件\n");
            fmt::print("  -h, --help         显示此帮助信息\n\n");
            fmt::print(style::title_color, "示例:\n");
            fmt::print("  listtree\n");
            fmt::print("  listtree /data/data/com.termux/files/home\n");
            fmt::print("  listtree -r ~/code\n");
            fmt::print("  listtree -r -a .\n");
            return 0;
        } else {
            // 视为路径参数
            root_path = fs::path(arg);
        }
    }

    // 检查路径是否存在
    std::error_code ec;
    if (!fs::exists(root_path, ec)) {
        fmt::print(stderr, style::error_color, "❌ 错误: 路径不存在: {}\n", root_path.string());
        return 1;
    }

    if (!fs::is_directory(root_path, ec)) {
        fmt::print(stderr, style::error_color, "❌ 错误: 路径不是目录: {}\n", root_path.string());
        return 1;
    }

    // 解析为绝对路径
    root_path = fs::absolute(root_path);

    // 打印标题
    fmt::print("\n");
    fmt::print(style::title_color, "  📂 目录树: ");
    fmt::print(fg(fmt::color::white) | fmt::emphasis::bold, "{}", root_path.string());
    fmt::print("\n");
    fmt::print(style::line_color, "  ");
    for (size_t i = 0; i < root_path.string().length() + 12; ++i) fmt::print("─");
    fmt::print("\n\n");

    // 打印根目录
    fmt::print(style::dir_color, "  📁 .\n");

    // 递归打印
    TreeStats stats;
    print_tree(root_path, "  ", recursive, stats, show_hidden);

    // 打印统计信息
    fmt::print("\n");
    fmt::print(style::stats_color, "  ── 统计 ──\n");
    fmt::print(style::stats_color, "  📁 目录: {}\n", stats.dirs);
    fmt::print(style::stats_color, "  📄 文件: {}\n", stats.files);
    if (stats.symlinks > 0) {
        fmt::print(style::stats_color, "  🔗 链接: {}\n", stats.symlinks);
    }
    if (stats.hidden > 0) {
        fmt::print(style::stats_color, "  🙈 隐藏: {}\n", stats.hidden);
    }
    fmt::print(style::stats_color, "  💾 总计: {}\n", format_size(stats.total_size));
    fmt::print("\n");

    return 0;
}
