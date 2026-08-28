$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
python tools/generate_clangd_db.py @args
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host ""
Write-Host "compile_commands.json 已生成。请在 Cursor 中执行: Clangd: Restart language server"
