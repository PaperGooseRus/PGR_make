#!/usr/bin/env python3
"""Contracts for PGR_Make 0.7 UI; source-level tests are not GTK runtime tests."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
s = (root / 'gui' / 'main.cpp').read_text(encoding='utf8')
css = (root / 'gui' / 'theme.hpp').read_text(encoding='utf8')
layout = (root / 'gui' / 'layout_settings.hpp').read_text(encoding='utf8')
for feature in (
    'pgr_gui_ux::load_mode', 'pgr_gui_ux::save_mode', 'on_mode_changed',
    'gtk_paned_new(GTK_ORIENTATION_VERTICAL)',
    'gtk_paned_new(GTK_ORIENTATION_HORIZONTAL)', 'restore_split_ratios',
    'gtk_notebook_append_page(GTK_NOTEBOOK(ui->results),result_page,gtk_label_new("Результат"))',
    'refresh_project_summary', 'check_configuration', 'pgr-invalid',
    'close_dialog_button', 'Сохранить изменения конфигурации', 'Не сохранять',
    'gtk_notebook_set_current_page(GTK_NOTEBOOK(ui->results),2)',
    'open_build_folder', 'run_built_executable', 'ui->history_box',
    'pgr_gui_ux::load_history', 'pgr_gui_ux::preflight',
):
    assert feature in s, feature
for selector in ('.pgr-project-summary', '.pgr-result-title', '.pgr-invalid', '.pgr-history-list'):
    assert selector in css
for field in ('projects_permille', 'output_permille'):
    assert field in layout
assert 'GtkWidget* version = label(PGR_APP_VERSION " · GTK4")' in s
assert '#define PGR_APP_VERSION "dev"' in s
assert 'Выход без сохранения' not in s
assert '"1. Компилятор' not in s
print('PASS: mode persistence, preflight, close dialog, summary, history, results, proportions')
