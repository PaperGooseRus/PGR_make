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
if exist "deploy\PGR\PGR_Make_GUI.exe" copy /Y "deploy\PGR\PGR_Make_GUI.exe" "C:\PGR\PGR_Make_GUI.exe"
if errorlevel 1 (popd & exit /b 1)
copy /Y "deploy\PGR\start_gui.bat" "C:\PGR\start_gui.bat"
if errorlevel 1 (popd & exit /b 1)
REM Install bundled GTK4 runtime only when none exists; leave user Pack tools intact.
if exist "deploy\PGR\Pack\GTK4\bin" if not exist "C:\PGR\Pack\GTK4" (
  xcopy "deploy\PGR\Pack\GTK4" "C:\PGR\Pack\GTK4\" /E /I /Q /Y >nul
  if errorlevel 1 (echo WARNING: Could not install GTK4 runtime. & popd & exit /b 1)
)
REM Managed catalog is installed only if absent; never overwrite custom manifests.
if not exist "C:\PGR\tools" mkdir "C:\PGR\tools"
copy /Y "deploy\PGR\tools\zip_guard.ps1" "C:\PGR\tools\zip_guard.ps1" >nul
copy /Y "deploy\PGR\tools\zip_guard.py" "C:\PGR\tools\zip_guard.py" >nul
REM The shipped catalog is updated with the application; custom Pack/catalog.json stays untouched.
copy /Y "deploy\PGR\tools\catalog.json" "C:\PGR\tools\catalog.json" >nul
if errorlevel 1 (echo ERROR: Could not update trusted tools catalog. & popd & exit /b 1)
if not exist "C:\PGR\Pack\catalog.json" copy "deploy\PGR\Pack\catalog.json" "C:\PGR\Pack\catalog.json" >nul
echo Installed CLI and GUI in C:\PGR\ (GTK4 runtime required).
REM Intentionally NEVER overwrite Pack tools or Project configurations.
popd
exit /b 0
