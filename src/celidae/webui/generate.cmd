@echo off
where node >nul 2>nul
if errorlevel 1 (
    echo Node.js is required to regenerate the Celidae visualizer assets.
    echo Install it from https://nodejs.org, then re-run this script.
    exit /b 1
)
node "%~dp0generate-template.js"
