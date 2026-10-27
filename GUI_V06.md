# PGR_Make GUI 0.6 — Two Panes + Compact Window Controls

GUI 0.6 is based on the Ash & Orange version of PGR_Make 0.5.
The builder backend, CLI, TUI, project registry and Pack Manager were not replaced.

## Layout

```
PGR_Make (native minimize / maximize / close buttons)
├── Header: project name, save status, theme
├── Toolbar: build mode / build / save / stop / toggle output
├── Vertical GtkPaned (drag to resize)
│   ├── Horizontal GtkPaned (drag to resize)
│   │   ├── Configuration and TUI commands (left)
│   │   └── Projects and Pack Manager (right)
│   └── Output panel (full width)
│       ├── Search / auto-scroll / copy / clear / expand
│       └── GtkNotebook: log / diagnostics / history
└── Status
```

New features:

- Drag both dividers; double areas remain independent of CLI settings.
- Top toolbar shows/hides the whole bottom output panel; the lower panel also has an Expand/Restore button.
- GUI remembers window size, divider positions, output visibility, log auto-scroll, active output tab and category.
- Configuration can be filtered by `All`, `Basic`, `Flags`, `Paths`, `CMake`, `Tools`, `Commands` (Russian labels in the actual GUI).
- Settings search matches individual widget labels, not just category headers.
- Output offers case-insensitive highlighted search (first 300 matches), clipboard copy, clear-visible-log and auto-scroll toggle.
- Native GTK4 title buttons are excluded from the large application button CSS rules, and the `windowcontrols` selector defines compact 24px buttons and 12px icons. The exact titlebar appearance remains platform/theme dependent.

### Keyboard shortcuts

- `Ctrl+B` — build current project
- `Ctrl+S` — save current project configuration
- `Ctrl+J` — show/hide output
- `Ctrl+F` — open output and focus log search

## Where layout preferences go

`C:\PGR\Project\.pgr_gui_layout` is a plain `key=value` file; it does **not** modify individual projects' `config.cfg`.
`C:\PGR\Project\.pgr_gui_theme` still stores Ash & Orange / light theme separately.

Existing configuration files continue to work. Invalid layout entries are ignored or clamped to safe ranges.

## Windows GTK4 build (MSYS2 UCRT64)

```cmd
cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DPGR_STATIC_WINDOWS=ON -DPGR_BUILD_GUI=ON -DPGR_REQUIRE_GUI=ON
cmake --build build-win --parallel
ctest --test-dir build-win --output-on-failure
```

For the GUI, the GTK4 development files and runtime are still required. Static MinGW runtime linkage does not make GTK4 DLLs disappear.

## Testing notes

- GCC/Linux: all 14 CTest tests pass, including GUI source/layout contracts and the new layout-preferences round-trip test.
- Windows native GTK4 GUI build and a visual run are **not verified** in this environment because the GTK4 SDK is absent.
- Test native minimize/maximize/close dimensions in Windows. The CSS change intentionally leaves the platform's titlebar controls outside application-wide button styling.
