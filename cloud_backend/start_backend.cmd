@echo off
cd /d "%~dp0.."
echo Starting ESP32 cloud backend...
echo Dashboard: http://localhost:3000
echo API:       http://localhost:3000/api/status
echo.
cd cloud_backend
if not exist node_modules call npm install
call npm start
echo.
echo Backend stopped. Press any key to close this window.
pause >nul
