---
name: wren-ui
description: "用 Wren（C++20、单 HWND、retained-mode 的 Windows 自绘 UI 框架，仓库 github.com/Elipese568/wren）写桌面界面：新建 Host/Widget 树、加控件、自定义 Widget、主题与 Mica/Acrylic 材质、焦点与键盘、MSAA 无障碍、动画节拍、以及用 --report / 截图验证。用户说「用 Wren 写界面」「给 Wren 加控件/页面」「自绘 Win32 UI」「接 Mica」「这个 gui/ui 目录」「Wren demo」时使用；在 MsixpkgInstaller 仓库里改 src/MsixpkgInstaller.Native/gui/ui（submodule）也用它。"
---

# 用 Wren 写界面

Wren 是安装器/更新器这类**固定尺寸、控件类型有限、要材质与无障碍**的小工具用的框架。
它不是通用 UI 库：

| 有 | 没有 |
|---|---|
| 单 HWND、retained Widget 树、绝对 DIP 布局 | 布局引擎（measure/arrange）、flex/grid |
| D2D + DirectComposition 硬件上屏，GDI+ 回退 | 数据绑定、声明式 DSL、XAML |
| Mica / Acrylic / Solid 自动降级 | 文本输入、IME |
| Tab 焦点链、键盘、MSAA `IAccessible` | 多窗口、动画时间线 |

**先记住三条**：一个窗口一个 `Host`；布局就是给每个 Widget 写 `bounds`（DIP）；颜色一律取
`Theme` 里的 token。

## 30 秒心智模型

```
Host（窗口 + 消息 + 焦点 + 帧节拍 + MSAA 根）
 └─ Panel(root, bounds = 整个客户区)
     ├─ Label / Button / Checkbox / ProgressBar / TextView / Table
     └─ 你自己的 Widget（继承 Widget，实现 paint）
```

- 控件**没有子 HWND**。`hit_test` 从最深的子节点往回问，不可交互的控件返 `nullptr`
  （让点击落到 Panel 上）。
- 绘制只发生在 `Widget::paint(Canvas&)` 里；`Canvas` 的坐标永远是 DIP。
- `Host` 负责失效重绘：改完控件属性调 `widget->invalidate()` 或
  `invalidate_with(rect)`。批量改一页的话 `host.layout_changed()`。
- 空闲时**不跑帧**；动画用 `host.set_animating(...)` 开关。

## 最小可运行程序

```cpp
#include "controls.h"
#include "host.h"

#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    auto root = std::make_unique<wren::Panel>();
    root->bounds = {0.0f, 0.0f, 420.0f, 160.0f};

    const wren::Theme theme = wren::make_theme(wren::Appearance::Auto, false);
    auto* title = root->emplace<wren::Label>(L"Hello Wren", theme.title);
    title->bounds = {24.0f, 20.0f, 320.0f, 32.0f};

    auto* button = root->emplace<wren::Button>(L"确定", [] { ::PostQuitMessage(0); });
    button->bounds = {24.0f, 72.0f, 120.0f, 32.0f};
    button->set_primary(true);
    button->set_default(true);   // Enter 触发

    wren::HostOptions options;
    options.title = L"Hello Wren";
    options.width_dip = 420;
    options.height_dip = 160;

    wren::Host host;
    if (!host.create(instance, std::move(root), options)) {
        return 1;
    }

    return host.run();
}
```

`Host::create` 内部会 `ShowWindow` + 建材质 + 建 presenter；失败返回 `false`，不要再自己
`CreateWindow`。`Host::run()` 是消息循环，返回退出码。

## 布局约定

- `bounds` 一律 DIP（96 DPI 下 1 DIP = 1 px）。DPI 由 Host 处理，**不要**自己乘比例。
- 固定页面尺寸就用 `HostOptions::width_dip/height_dip`；`Host` 会按 DPI 反推窗口尺寸。
- 布局函数写法：一页一个 `void layout_xxx(Widget& root, float width, float height)`，里面
  直接赋 `bounds`。需要跟随窗口尺寸时在 `WM_SIZE` 之后调 `host.layout_changed()`。
- 内边距/间距习惯：页边距 18–24，卡片圆角 `theme.radius_card`（12），控件圆角
  `theme.radius_control`（6），控件高 30–32。
- 文本容器不要给死高度：中文行高随字号变化，`TextView` 会按行模型滚动。

## 控件速查

```cpp
auto* l = panel->emplace<wren::Label>(L"标题", theme.heading);
l->set_tone(wren::LabelTone::Secondary);   // Primary/Secondary/Disabled/Accent/Warning
l->set_ellipsis(true);

auto* b = panel->emplace<wren::Button>(L"安装", [&] { start(); });
b->set_primary(true);        // 强调色实心
b->set_default(true);        // Enter 触发（一页只设一个）
b->set_icon(wren::IconId::Download);

auto* c = panel->emplace<wren::Checkbox>(L"我同意");
c->set_on_changed([](bool checked) { /* … */ });
c->set_checked(true);

auto* p = panel->emplace<wren::ProgressBar>();
p->set_value(30);            // 整数百分比（无障碍按此报告）
p->set_value_f(30.4f);       // 亚百分比：高刷新率下进度条平滑移动
p->set_marquee(true);        // 不确定进度

auto* tv = panel->emplace<wren::TextView>(text, theme.body);   // 只读、可滚动、Ctrl+A/C
tv->set_show_scrollbar(true);

auto* t = panel->emplace<wren::Table>();
t->set_columns({{L"包名", 220.0f}, {L"版本", 90.0f}});          // Column{title,width}
t->set_rows({{L"Contoso.Studio", L"1.0.0"}});
t->set_on_selection_changed([](int row) { /* … */ });
```

`Table` 自带表头、行高亮、滚动、方向键/Home/End/PageUp/PageDown，并且**每一行都是 MSAA
子对象**。

## 自定义 Widget

装饰容器（卡片、分区）就应该自己写，框架故意不提供：

```cpp
class Card : public wren::Widget {
public:
    void paint(wren::Canvas& canvas) override {
        const wren::Theme& t = theme();
        canvas.fill_round_rect(bounds, t.radius_card, t.card);
        canvas.stroke_round_rect(bounds, t.radius_card, t.card_border, 1.0f);
    }

    wren::Role accessible_role() const override { return wren::Role::Panel; }
    wren::Widget* hit_test(float, float) override { return nullptr; }  // 让点击穿透
};
```

要点：
- 只画，不布局子节点时**不要**继承 `Panel`；`Panel` 会做子节点绘制和命中分派。
- `hit_test` 默认实现已经处理可见/禁用/越界；不可交互就覆写成 `nullptr`。
- 想让控件进 Tab 链：`tab_stop = true`；再实现 `on_key`（`case VK_SPACE`…）。
- 需要无障碍：覆写 `accessible_role/name/value/state`，状态变化后调
  `host->notify_accessible(this, EVENT_OBJECT_STATECHANGE)`。

## 主题与材质（不要硬编码颜色）

```cpp
const wren::Theme& t = host.theme();      // 也会随主题切换更新
canvas.fill_round_rect(r, t.radius_card, t.card);
canvas.draw_text(text, r, t.body, t.text);

host.set_appearance(wren::Appearance::Dark);      // Light / Dark / HighContrast / Auto
host.set_material(wren::MaterialPreference::Solid); // Auto / Mica / Acrylic / Solid
```

- 材质可见时（Mica/Acrylic）卡片是**半透明**表面，文字才能透出材质；Solid 下必须不透明，
  否则会发灰 —— 这是 `make_theme(appearance, material_visible)` 的第二个参数。
- 高对比度下材质会自动关闭、`theme` 会换成系统色；不要再叠加自定义 alpha。
- 需要当前材质/外观做判断时用 `host.material_kind()`、`host.appearance()`。

## 焦点与键盘

- Tab/Shift+Tab 顺序 = **Widget 创建顺序**（对应宿主 `ControlSpec` 的原顺序）。所以先建
  标题再建输入，不要后补。
- `Button::set_default(true)` 让 Enter 落到它；一页只设一个。
- Esc 走 `HostOptions::on_escape`（为空时 Host 直接关窗）。
- 焦点环由 `Widget::paint_focus_ring` 画（1px 强调色 + 2px 内缩），不要自己画红色边框。
- `host.set_tick_handler([&]{ app.tick(); })` 是每帧回调；`host.schedule_once(ms, cb)` 是一次性
  定时，不占动画节拍。

## 动画与帧率

```cpp
host.set_animating(true, 0);   // 跟随刷新率：每个合成器 tick 一帧（180Hz → 180fps）
host.set_animating(true, 16);  // 约 60fps（按实测 tick 周期折算）
host.set_animating(false);     // 停帧，回到纯消息等待（空闲 0% CPU）
```

- 动画期间 `invalidate()` 会直接返回（避免一帧画两遍）；改属性不需要手动调用它。
- 每帧的 `dt` 自己算：用 `std::chrono::steady_clock`，**不要**用 `GetTickCount64`（15.6ms
  粒度，高刷新率下步长会跳）。
- 进度条用 `set_value_f` 才能平滑；`set_value(int)` 只给无障碍/文本用。

## 验证（改完必须跑）

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DWREN_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
build\Release\wren-tests.exe                     # 纯逻辑单测，必须 ALL PASS

# 无人值守跑一遍并导出 MSAA + 帧统计
$p = Start-Process build\Release\wren-demo.exe -ArgumentList @(
  '--report', "$env:TEMP\r.txt", '--auto', '--exit-after', '2500') -Wait -PassThru
Get-Content "$env:TEMP\r.txt" | Select-String 'frames=|refresh_hz|childCount|presenter.kind'
```

`--report` 走真实 `OBJID_CLIENT` → `WM_GETOBJECT` → MSAA。看到 `childCount`、`frames=`、
`refresh_hz` 才算真的能上屏。GUI 程序是 WIN32 子系统：PowerShell 里必须 `Start-Process
-Wait`，直接 `&` 不会等待。

## 常见错误

| 症状 | 原因 / 修法 |
|---|---|
| 窗口空白 | `Host::create` 返回 false 被忽略；或者 root 的 `bounds` 是 0×0（Panel 也要给 bounds） |
| 点击没反应 | 控件 `enabled=false`，或者上层 Widget 的 `hit_test` 抢走了命中；Label 必须返回 `nullptr` |
| 中文显示成方框 | 没加 `/utf-8`（源文件是 UTF-8 中文）；或字体 family 写错 |
| 文字发虚 | 不要期待 ClearType：Wren 全框架灰度 AA，透明表面才能正确合成 |
| 进度条不动 | 忘了 `set_animating(true)`，或用了 `set_value(int)` 而动画期间需要 `set_value_f` |
| 透明处是黑的 | presenter 没走 GPU 路径，或窗口重定向位图没清成 alpha=0（框架已处理，别自己画背景） |
| 高 DPI 下错位 | 布局里写了像素值；所有 `bounds` 必须是 DIP |
| UIA 看不到控件 | 控件 `visible=false`/在屏幕外；或者 Table 行没有 `set_rows` |
| 编译报 gdiplus 错误 | 消费方没开 `/utf-8`（Wren 已 `PUBLIC` 传播，自建工程时注意） |

## 代码约定（本仓库）

- 提交前跑钩子：`pwsh -File scripts/setup-hooks.ps1`，clang-format 会格式化暂存文件。
- 注释只写一行「为什么/坑」，设计论证去 `README.md` / `docs/design.md`；不要写「本函数用于…」。
- 源文件和注释都是 UTF-8（无 BOM）；`#include "..."` 在前，系统头其次，标准库最后。
- 不要引入第三方依赖；不要用异常做控制流（Win32 回调里尤其）。

## 需要细节时

- `references/api.txt`：按头文件整理的完整签名清单。
- `references/recipes.md`：可整段粘贴的片段（设置页、带表格的列表页、自定义控件、主题切换、无障碍）。
- `references/pitfalls.md`：渲染/材质/节拍/无障碍的坑与排查路径。
- 仓库：<https://github.com/Elipese568/wren>；设计稿 `docs/design.md`；演示 `demo/wren_demo.cpp`。
