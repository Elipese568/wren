#pragma once

// MSAA（IAccessible）暴露（docs/design.md §6）：
// 根对象 = 客户区，子对象 = 可见 Widget + Table 的每一行。

#include <Windows.h>

#include <memory>

struct IAccessible;

namespace wren {

class Host;

struct AccessibleReleaser {
    void operator()(IAccessible* value) const noexcept;
};

using AccessiblePtr = std::unique_ptr<IAccessible, AccessibleReleaser>;

AccessiblePtr create_accessibility_root(Host* host);

}  // namespace wren
