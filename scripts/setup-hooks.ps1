# 启用仓库自带的 git 钩子（clang-format 自动格式化）
#
#   pwsh -File scripts/setup-hooks.ps1
#
# 等价于：git config core.hooksPath .githooks，并检查 clang-format 是否可用。

$ErrorActionPreference = 'Stop'

$root = (git rev-parse --show-toplevel).Trim()
Set-Location $root

git config core.hooksPath .githooks
Write-Host "core.hooksPath = .githooks"

$formatter = $env:CLANG_FORMAT
if (-not $formatter) {
    $formatter = (Get-Command clang-format -ErrorAction SilentlyContinue).Source
}
if (-not $formatter) {
    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe',
        'C:\Program Files\LLVM\bin\clang-format.exe'
    )
    $formatter = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if ($formatter) {
    Write-Host "clang-format: $formatter"
    & $formatter --version
} else {
    Write-Warning '未找到 clang-format；提交时会跳过格式化（可设置 CLANG_FORMAT 环境变量）。'
}
