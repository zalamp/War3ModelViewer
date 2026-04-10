@echo off
cd /d "%~dp0"
title War3ModelViewer - MDX解析服务

if not exist "node_modules" (
    echo 正在安装依赖...
    call npm install
)

:: 重定向输出到 nul 可避免 Qt 捕获乱码，但黑框内不会有文字
:: 如果你想看到黑框内的日志，去掉 "> nul 2>&1" 即可
node service.js

:: 如果 node 进程退出，暂停一下（方便查看错误）
pause
