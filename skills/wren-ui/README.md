# wren-ui skill

指导 AI 用 Wren 写界面的技能包（Claude/DSH 的 `SKILL.md` 格式），随仓库发布。

| 文件 | 内容 |
|---|---|
| [`SKILL.md`](SKILL.md) | 入口：心智模型、最小程序、控件/布局/主题/焦点/动画/验证、常见错误、代码约定 |
| [`references/api.txt`](references/api.txt) | 按头文件整理的完整签名 |
| [`references/recipes.md`](references/recipes.md) | 可粘贴的片段（页面骨架、表格、自定义控件、安装流程、主题切换、MSAA） |
| [`references/pitfalls.md`](references/pitfalls.md) | 现象 → 排查 → 根因 |

## 安装到 DSH

技能目录是用户级的，仓库里的这份是源：

```powershell
$dst = Join-Path $env:USERPROFILE '.dsh\skills\wren-ui'
Remove-Item $dst -Recurse -Force -ErrorAction SilentlyContinue
Copy-Item skills\wren-ui $dst -Recurse
```

装好后在会话里就能按名字加载（`wren-ui`），新开会话才会刷新技能列表。

## 维护

改完 `SKILL.md` 或 references 后重新执行上面的复制；`api.txt` 在头文件有新增/改签名的
PR 里手动同步（没有自动生成脚本）。
