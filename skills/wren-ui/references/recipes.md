# Wren 配方（可整段粘贴）

约定：所有数字是 DIP；页面宽高来自 `HostOptions`（下面按 720×480 的安装器页面写）。

## 1. 一页的标准骨架

```cpp
namespace {

constexpr float kPageWidth = 720.0f;
constexpr float kPageHeight = 480.0f;
constexpr float kMargin = 18.0f;

struct Page {
    wren::Host* host = nullptr;
    wren::Panel* root = nullptr;
    wren::Label* status = nullptr;
    wren::ProgressBar* progress = nullptr;
    wren::Button* back = nullptr;
    wren::Button* next = nullptr;
    float accum = 0.0f;
    std::chrono::steady_clock::time_point last_tick{};
};

void layout_page(Page& page) {
    page.progress->bounds = {kMargin, kPageHeight - 96.0f, kPageWidth - kMargin * 2.0f, 8.0f};
    page.status->bounds = {kMargin, kPageHeight - 74.0f, 420.0f, 20.0f};
    page.back->bounds = {kPageWidth - 264.0f, kPageHeight - 62.0f, 110.0f, 32.0f};
    page.next->bounds = {kPageWidth - 136.0f, kPageHeight - 62.0f, 118.0f, 32.0f};
}

}  // namespace
```

要点：`root->bounds` 必须覆盖整个客户区，否则子控件一离开 root 矩形就被裁剪。

## 2. 表格 + 选择联动

```cpp
auto* table = page.root->emplace<wren::Table>();
table->bounds = {kMargin, 120.0f, kPageWidth - kMargin * 2.0f, 220.0f};
table->set_columns({{L"包名", 320.0f}, {L"版本", 110.0f}, {L"状态", 120.0f}, {L"ID", 90.0f}});
table->set_rows({
    {L"Contoso.Studio", L"1.0.0", L"待安装", L"A1F2"},
    {L"Contoso.Term", L"4.4.4", L"已安装", L"F5C9"},
});
table->set_on_selection_changed([&page](int row) {
    page.next->enabled = row >= 0;
    page.next->invalidate();
    page.status->set_text(row >= 0 ? L"已选择第 " + std::to_wstring(row + 1) + L" 行" : L"未选择");
    page.status->invalidate();
});
```

`Table` 自带方向键/Home/End/PageUp/PageDown 与行级 MSAA；不要自己再包一层 ListView。

## 3. 自定义装饰控件（Card / 状态徽章）

```cpp
class Badge : public wren::Widget {
public:
    void set_tone(wren::LabelTone tone) {
        tone_ = tone;
        invalidate();
    }

    void paint(wren::Canvas& canvas) override {
        const wren::Theme& t = theme();
        const bool warning = tone_ == wren::LabelTone::Warning;
        canvas.fill_round_rect(bounds, bounds.height * 0.5f, warning ? t.warning_surface : t.control);
        canvas.draw_text(text, bounds.inset(10.0f), t.caption, warning ? t.warning : t.text_secondary);
    }

    wren::Role accessible_role() const override { return wren::Role::Label; }
    wren::Widget* hit_test(float, float) override { return nullptr; }  // 点击穿透到 Panel

private:
    wren::LabelTone tone_ = wren::LabelTone::Secondary;
};
```

- 装饰容器**不要**继承 `Panel`（那样会参与命中分派）。
- `text` 是 `Widget` 的成员，直接赋值 `badge->text = L"待安装";` 再 `invalidate()`。

## 4. 安装流程：动画 + 结束停帧

```cpp
void begin_install(Page& page) {
    page.next->enabled = false;
    page.next->invalidate();
    page.accum = 0.0f;
    page.last_tick = std::chrono::steady_clock::now();
    page.host->set_animating(true, 0);   // 跟随刷新率（--fps 语义由调用方决定）

    page.host->set_tick_handler([&page] {
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - page.last_tick).count();
        page.last_tick = now;

        page.accum = std::min(100.0f, page.accum + static_cast<float>(dt) * 26.0f);
        page.progress->set_value_f(page.accum);                       // 平滑绘制
        page.progress->set_value(static_cast<int>(page.accum));       // 无障碍读整数

        if (page.accum >= 100.0f) {
            page.host->set_tick_handler({});
            page.host->set_animating(false);                          // 停帧，CPU 回 0
            page.next->enabled = true;
            page.next->invalidate();
        }
    });
}
```

用 `steady_clock` 而不是 `GetTickCount64`：后者 15.6ms 粒度，高刷新率下步长会跳。

## 5. 主题 / 材质切换

```cpp
void cycle_theme(Page& page) {
    const bool dark = page.host->appearance() == wren::Appearance::Dark;
    page.host->set_appearance(dark ? wren::Appearance::Light : wren::Appearance::Dark);
    page.theme_label->set_text(dark ? L"浅色" : L"深色");
    page.theme_label->invalidate();
}

void cycle_material(Page& page) {
    using wren::MaterialPreference;
    const MaterialPreference next =
        page.host->material_preference() == MaterialPreference::Auto ? MaterialPreference::Solid
                                                                     : MaterialPreference::Auto;
    page.host->set_material(next);
}
```

`set_appearance` 后 Host 会重建 theme 并调所有 Widget 的 `on_theme_changed()`；控件在
`paint` 里每次取 `theme()` 就不需要额外处理。硬编码颜色的控件在这里会露馅。

## 6. 无障碍：自定义控件补 MSAA

```cpp
wren::Role accessible_role() const override { return wren::Role::Checkbox; }
std::wstring accessible_value() const override { return checked_ ? L"已选中" : L"未选中"; }

uint32_t accessible_state() const override {
    uint32_t state = wren::Widget::accessible_state();
    if (checked_) {
        state |= wren::kStateChecked;
    }
    return state;
}

void set_checked(bool value) {
    checked_ = value;
    invalidate();
    if (host != nullptr) {
        host->notify_accessible(this, EVENT_OBJECT_STATECHANGE);   // 读屏器才会播报
    }
}
```

`Host` 通过 `WM_GETOBJECT` 暴露根对象；子对象顺序 = Widget 创建顺序，Table 行追加在其
控件之后。改完用 `--report` 看 `childCount` 和每个子对象的 role/state。

## 7. 验证脚本

```powershell
# 构建 + 单测
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DWREN_WARNINGS_AS_ERRORS=ON
cmake --build build --config Release
build\Release\wren-tests.exe

# 无人值守截图/报告（GUI 必须 Start-Process -Wait）
$report = "$env:TEMP\wren.txt"
Start-Process build\Release\wren-demo.exe -Wait -PassThru -ArgumentList @(
    '--report', $report, '--theme', 'dark', '--material', 'mica',
    '--auto', '--exit-after', '2500') | Out-Null
Get-Content $report | Select-String 'childCount|frames=|refresh_hz|presenter.kind'
```

判定标准：`frames > 0`；GPU 路径下 `refresh_hz` 接近显示器刷新率；`childCount` 等于预期
控件数。想对比软件路径就加 `--presenter gdi`。
