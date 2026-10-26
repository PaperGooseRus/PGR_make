This directory stores only PGR_Make metadata, NOT source code.
On startup, choose a project or create one. For a project called Calculator:

  Project\Calculator\project.pgr         source directory registration
  Project\Calculator\config.cfg          compiler and build settings
  Project\Calculator\profiles\           optional build profiles
  Project\Calculator\work.log            build diagnostics from TUI
  Project\Calculator\generated_cmake\    temporary CMakeLists files

Never place hand-maintained files in generated_cmake (it is regenerated).
Existing project metadata is preserved when updating PGR_Make.exe.
