#pragma once

// 文本换行。先按 DIP 算好行、再逐行绘制，TextView 才能按行滚动与定位。

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace wren {

struct WrappedLine {
    std::wstring text;
    float width = 0.0f;
};

using MeasureWidth = std::function<float(std::wstring_view)>;

/// 按 max_width（DIP）贪心换行。显式 '\n' 强制断行；CJK 逐字可断，
/// 拉丁文按词断（超长单词再逐字拆）。返回的每行已经去掉行尾空格。
std::vector<WrappedLine> wrap_text(std::wstring_view text, float max_width, const MeasureWidth& measure);

/// 是否为可以逐字断行的 CJK 字符（含全角标点、假名、谚文）。
bool is_breakable_cjk(wchar_t c);

}  // namespace wren
