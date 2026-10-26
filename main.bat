@echo off
setlocal
call "%~dp0build_windows.bat"
if errorlevel 1 exit /b 1
"%~dp0deploy\PGR\PGR_Make.exe" %*
exit /b %errorlevel%
