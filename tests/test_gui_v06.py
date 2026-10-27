#!/usr/bin/env python3
"""Static regression contracts for the v0.6 GUI. Run even without GTK SDK."""
from pathlib import Path
s = (Path(__file__).resolve().parents[1] / 'gui' / 'main.cpp').read_text(encoding='utf8')
css = (Path(__file__).resolve().parents[1] / 'gui' / 'theme.hpp').read_text(encoding='utf8')
checks = [
    'gtk_paned_new(GTK_ORIENTATION_VERTICAL)',
    'gtk_paned_new(GTK_ORIENTATION_HORIZONTAL)',
    'gtk_paned_set_start_child(GTK_PANED(vertical), paned)',
    'gtk_paned_set_end_child(GTK_PANED(vertical), output)',
    'gtk_paned_set_start_child(GTK_PANED(paned), left)',
    'gtk_paned_set_end_child(GTK_PANED(paned), right)',
    'gtk_box_append(GTK_BOX(output), ui->results)',
    'toggle_output', 'expand_output', 'on_autoscroll', 'on_log_search',
    'copy_log', 'clear_log', 'gtk_text_iter_forward_search',
    'gtk_widget_set_visible(output, ui->layout.output_visible)',
    'schedule_layout_save', 'save_layout_now',
    'gtk_window_set_default_size(GTK_WINDOW(ui->window)',
    'gtk_drop_down_new_from_strings(groups)', 'update_settings_filter',
    'GDK_KEY_j', 'GDK_KEY_b', 'GDK_KEY_s', 'GDK_KEY_f',
    'gtk_notebook_set_current_page(GTK_NOTEBOOK(ui->results), ui->layout.output_page)',
]
for c in checks:
    assert c in s, f'Missing: {c}'
assert 'gtk_box_append(GTK_BOX(left), ui->results)' not in s, 'Output still inside the left panel'
assert '.pgr-app button {' in css, 'Workspace buttons not scoped'
assert '\nbutton {\n' not in css, 'Global button style can enlarge native title buttons'
assert 'windowcontrols button' in css and 'min-width: 24px' in css
assert '.pgr-vertical-paned > separator' in css
print('PASS: two splitters, full-width output, preferences, search, compact window controls')
