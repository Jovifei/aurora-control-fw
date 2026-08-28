@echo off
setlocal
cd /d "%~dp0.."
python tools\generate_clangd_db.py %*
if errorlevel 1 exit /b 1
echo.
echo compile_commands.json 已生成。请在 Cursor 中执行:
echo   Clangd: Restart language server
endlocal
