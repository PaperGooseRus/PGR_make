@echo off
setlocal
pushd "%~dp0"
if not exist "deploy\PGR\PGR_Make.exe" (
  echo PGR_Make.exe missing. Run build_windows.bat first.
  popd
  exit /b 1
)
if not exist "C:\PGR" mkdir "C:\PGR"
if errorlevel 1 (echo Cannot create C:\PGR. Check permissions. & popd & exit /b 1)
if not exist "C:\PGR\Pack" mkdir "C:\PGR\Pack"
if not exist "C:\PGR\Project" mkdir "C:\PGR\Project"
copy /Y "deploy\PGR\PGR_Make.exe" "C:\PGR\PGR_Make.exe"
if errorlevel 1 (popd & exit /b 1)
echo Installed C:\PGR\PGR_Make.exe
REM Intentionally NEVER overwrite Pack tools or Project configurations.
popd
exit /b 0
