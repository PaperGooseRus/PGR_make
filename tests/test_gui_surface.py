#!/usr/bin/env python3
"""Source contract tests; no GTK4 headers or display necessary."""
from pathlib import Path
s = (Path(__file__).resolve().parents[1] / "gui" / "main.cpp").read_text(encoding="utf8")
required = [
    "gtk_paned_new(GTK_ORIENTATION_HORIZONTAL)",
    "gtk_paned_set_start_child(GTK_PANED(paned), left)",
    "gtk_paned_set_end_child(GTK_PANED(paned), right)",
    "gtk_drop_down_new_from_strings", "gtk_check_button_new_with_label",
    "gtk_text_view_new", "gtk_file_dialog_select_folder", "gtk_file_dialog_open",
    "package_dirs", "save_settings", "load_project", "project_activated",
]
for token in required:
    assert token in s, token
for number in list(range(1, 39)):
    assert (f'"{number}.' in s) or (f'"{number}–' in s) or (f' {number}–' in s), number
print("PASS: GtkPaned layout, 38 TUI entries, widgets, GTK4 file dialogs")

# Regression: UTF-8 labels must use escaped newlines inside C++ string literals.
assert 'недоступен")+"]\\n"+' in s, 'broken installed-tool label string'
assert 'pkg.id+"\\n"+pkg.url' in s, 'broken available-package label string'
assert 'pkg.id+"\n"+pkg.url' not in s, 'raw newline inside package string'
