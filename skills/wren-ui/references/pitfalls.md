# Wren 排查手册

按「现象 → 先查什么 → 根因/修法」写。性能数字都是本机（RTX 4060 Ti / 180Hz）实测。

## 上屏与材质

**窗口透出来的地方是黑的 / 发白**

1. 看 `--report` 里的 `presenter.kind`：`Gdi` 说明没走硬件路径。
2. GPU 路径下透明区露出的是 DWM 的材质，但窗口自己的重定向位图必须是 alpha=0。
   框架用 `PatBlt(BLACKNESS)` 清过；**不要在 root Panel 上画不透明背景**，那会把
   Mica 整个盖住。
3. Solid 材质下这就是预期（没有透明背景），用 `host.material_kind()` 判断。

**Mica 不生效但窗口没坏**

降级链是 Mica → Acrylic → Solid，任何一步失败都继续往下走：Windows 10、远程桌面、
系统关闭透明效果、高对比度都会落到 Solid。看 `host.material_kind()`，这是**正常回退**，
不是 bug。

**动画很卡 / 撕裂**

- 组合交换链的 `Present(1)` **不做反压**。用 `WM_TIMER` 驱动就是 15.6ms 粒度 + 不
  对齐 vblank。框架用 `DCompositionWaitForCompositorClock`，别自己改回定时器。
- 反过来，忘了等合成器时钟会 900+ fps 空转（CPU 打满、不撕裂但费电）。
- 窗口改尺寸后画面模糊：交换链没有 `ResizeBuffers`。`Presenter::resize` 必须实现。

**帧率数字怎么读**

`host.frame_stats()`：`interval_ms` 是相邻帧间隔中位数，`refresh_hz` 由它反推；
`dropped` 数的是间隔超过期望值 1.5 倍的次数。启动前 5 帧不计入（DWM 预热）。
如果 `render_ms + present_ms` 已经接近 `interval_ms`，就是 CPU/GPU 到顶了，
先砍控件数量或降 `set_animating` 的频率。

## 文字

**字发虚、有彩边**

Wren 全框架是 DirectWrite 灰度 AA，不支持 ClearType（透明表面上 ClearType 会出彩边、
写不对 alpha）。看到彩边说明有人另加了 GDI/GDI+ 的 ClearType 路径。

**同一屏里标题和正文基线差半个像素**

历史上出现过：两套文字后端各自排版。现在测量与绘制共用同一份 `IDWriteTextLayout`，
`Canvas::measure_text*` 与 `draw_text` 必须来自同一 `TextStyle`。

**文字整行消失**

GDI+ 在布局矩形高度小于字体行高时会**整行不画**（不是裁切）。给文本容器的高度要
≥ `canvas.line_height(style) * 1.35`（`TextView` 内部这么算）。

**中文变成方框**

消费方没开 `/utf-8`，或者选了一个没有中文字形的 family。`TextStyle::family` 默认
`Microsoft YaHei UI`。

## 交互

| 现象 | 先查 |
|---|---|
| 点按钮没反应 | 上层 Widget 的 `hit_test` 是否返回了非 null（装饰控件必须返回 nullptr） |
| Tab 走不到某个控件 | `tab_stop`、`visible`、`enabled` 三个都为真才可聚焦 |
| Enter 不触发 | 一页只能有一个 `set_default(true)`，且它必须 enabled |
| 鼠标拖拽状态卡住 | `on_mouse` 返回 true 才会被捕获；`Down` 里记得处理 `Up`（框架会补发） |
| 滚动条拖不动 | `scroll_for_thumb_y` 的 area 与 `scrollbar_metrics` 用的矩形必须一致 |

**裁剪不平衡**：`push_clip` 必须与 `pop_clip` 成对，且 early return 只能放在 push 之前
或 pop 之后。失衡会让后续所有控件被错误裁剪（表现为"某块区域不刷新"）。

## 数据与边界

- 空 `rows` / 空 `lines_` 上取 `back()`：Table/TextView 已经判空，自定义控件别忘。
- `ProgressBar::set_value(int)` 只给无障碍；动画期间要 `set_value_f(float)`，否则
  180fps 下进度条按 1% 台阶跳。
- Table 只遍历可见行，几千行不会慢；但 `set_rows` 每次都会重新分配，改数据后
  调一次即可。

## 无障碍

**`childCount=0`**：`WM_GETOBJECT` 的 `LPARAM` 在 64 位下可能被零扩展成
`0x00000000FFFFFFFC`，必须 `static_cast<LONG>(lparam) == OBJID_CLIENT` 比较，否则永远
落到 `DefWindowProc` 的默认对象。

**UIA 看不到 Table 行**：行是控件之后的额外子对象，`Table::accessible_row_count()`
返回行数、`accessible_value()` 返回当前行文本。

**读屏器不播报状态变化**：改完状态要 `host->notify_accessible(this, EVENT_OBJECT_* )`。

## 构建

- **必须 `/utf-8`**：头文件注释是 UTF-8 中文，MSVC 按 ANSI 代码页读会吞掉下一行的
  `#include`，随后 gdiplus 头直接编译失败。`Wren::wren` 已经把 `/utf-8` 设为 `PUBLIC`；
  自己写 CMake 时别漏。
- 链接库：`gdiplus d2d1 dwrite d3d11 dcomp dxgi dwmapi oleacc oleaut32 ole32 user32
  gdi32 advapi32`，`Wren::wren` 已 `PUBLIC` 带上。
- clang-format 钩子没跑：`pwsh -File scripts/setup-hooks.ps1`（`core.hooksPath` 是
  每克隆独立的配置）。

## 快速定位顺序

1. `--presenter gdi` 能不能跑通？能 → 问题在 D3D11/DComp/材质；不能 → 问题在绘制或
   生命周期。
2. `--report` 有没有 `frames=` 和 `childCount`？没有 → `Host::create` 失败或 MSAA 桥没接上。
3. `frame_stats()` 的 `render_ms` / `present_ms` 是否已经接近 `interval_ms`？是 → 性能，
   不是功能。
4. 同一份代码在 96 DPI 正常、150% 错位 → 找硬编码像素值（必须全 DIP）。
