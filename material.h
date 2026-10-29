#pragma once

// 窗口背景材质（docs/design.md §4）：Mica / Acrylic（未文档化 API 隔离在本文件）/ Solid。
// Widget 不感知材质，只画半透明或实色表面。

#include "canvas.h"

#include <memory>

namespace wren {

enum class MaterialKind { Solid, Acrylic, Mica };
enum class MaterialPreference { Auto, Solid, Acrylic, Mica };

class Material {
public:
    virtual ~Material() = default;
    virtual MaterialKind kind() const = 0;
    /// 成功返回 true；任何一步失败都必须返回 false（Host 会依次回退）。
    virtual bool apply(HWND hwnd, bool dark) = 0;
    virtual Color tint() const = 0;
};

std::unique_ptr<Material> make_material(MaterialPreference preference);

/// 切换标题栏亮暗。Windows 11 用属性 20，旧版本用 19；两个都失败就忽略。
void apply_dark_title_bar(HWND hwnd, bool dark);

const wchar_t* material_name(MaterialKind kind);

}  // namespace wren
