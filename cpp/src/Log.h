#ifndef LOG_H
#define LOG_H

#include <string>
#include <vector>

// 提取过程中的警告/被守卫的异常记录。每个条目 = 一个子系统（stage）在提取时
// 遇到越界数据 / 跳过的几何 / 解码失败等事件，供 Godot 端以弹窗展示。
struct Warning {
    std::string stage;   // level_script / geo / dl / texture / collision / extract
    std::string message; // 详细描述（含地址、段大小等上下文）
};

// 每次提取一个的警告日志：LevelExtractor 持有它，run() 时清空，结束时拷入
// Result::warnings。各子系统（level script VM / geo layout / DL 解释器）通过
// 构造时传入的引用记录事件（避免全局状态）。
class WarningLog {
    std::vector<Warning> entries_;

public:
    void add(const std::string &stage, const std::string &message) {
        entries_.push_back({stage, message});
    }
    void clear() { entries_.clear(); }
    bool empty() const { return entries_.empty(); }
    size_t size() const { return entries_.size(); }
    const std::vector<Warning> &entries() const { return entries_; }
};

#endif /* LOG_H */
