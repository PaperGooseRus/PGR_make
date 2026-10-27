@echo off
setlocal
REM GTK4 for Windows is generally dynamically linked.
REM Place GTK4 runtime in C:\PGR\Pack\GTK4 (bin\*.dll, share\...)
set "PGR_HOME=%~dp0"
if exist "%PGR_HOME%Pack\GTK4\bin" set "PATH=%PGR_HOME%Pack\GTK4\bin;%PATH%"
if exist "%PGR_HOME%Pack\GTK4\share\glib-2.0\schemas" set "GSETTINGS_SCHEMA_DIR=%PGR_HOME%Pack\GTK4\share\glib-2.0\schemas"
if exist "%PGR_HOME%Pack\GTK4\share" set "XDG_DATA_DIRS=%PGR_HOME%Pack\GTK4\share"
if not exist "%PGR_HOME%PGR_Make_GUI.exe" (
  echo ERROR: PGR_Make_GUI.exe not found. Build GUI with GTK4 first.
  exit /b 1
)
"%PGR_HOME%PGR_Make_GUI.exe"
exit /b %errorlevel%
