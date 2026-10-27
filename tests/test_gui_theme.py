#!/usr/bin/env python3
"""Source-only GTK4 style regression tests, independent of the GTK runtime."""
from pathlib import Path
import re

base = Path(__file__).resolve().parents[1]
theme = (base / 'gui' / 'theme.hpp').read_text(encoding='utf-8')
gui = (base / 'gui' / 'main.cpp').read_text(encoding='utf-8')
assert 'pgr_gui_theme::stylesheet(ui->dark_theme)' in gui
assert 'gtk_css_provider_load_from_data' in gui
assert 'gtk_style_context_add_provider_for_display' in gui
assert 'toggle_theme' in gui and '.pgr_gui_theme' in gui
assert 'gtk_paned_set_start_child(GTK_PANED(paned), left)' in gui
assert 'gtk_paned_set_end_child(GTK_PANED(paned), right)' in gui
assert 'pgr-diagnostic-error' in gui and 'pgr-diagnostic-warning' in gui
assert 'gtk_spinner_start' in gui and 'gtk_spinner_stop' in gui

dark = theme.split('kDarkPalette = R"PGR_CSS(', 1)[1].split(')PGR_CSS"', 1)[0]
light = theme.split('kLightPalette = R"PGR_CSS(', 1)[1].split(')PGR_CSS"', 1)[0]
components = theme.split('kComponents = R"PGR_CSS(', 1)[1].split(')PGR_CSS"', 1)[0]
for name, css, expected in [
    ('dark', dark, ['#1A1A1A', '#141413', '#30302E', '#D97757', '#FAF9F5']),
    ('light', light, ['#FFFFFF', '#F7F7F8', '#F4F4F4', '#0D0D0D']),
]:
    for color in expected:
        assert color in css, (name, color)
    names = re.findall(r'@define-color\s+(pgr_\w+)\s+#[0-9A-Fa-f]{6}\s*;', css)
    assert len(names) == len(set(names)) >= 14, (name, names)
    assert set(names) == set(re.findall(r'@define-color\s+(pgr_\w+)', dark)), name
    assert css.count('{') == css.count('}'), name
used = set(re.findall(r'@(pgr_\w+)', components))
defined = set(re.findall(r'@define-color\s+(pgr_\w+)', dark))
assert used <= defined, sorted(used-defined)
assert components.count('{') == components.count('}')
for cls in ('pgr-header', 'pgr-sidebar', 'pgr-toolbar', 'pgr-section',
            'pgr-project-list', 'pgr-log', 'pgr-results', 'pgr-diagnostic-error'):
    assert '.'+cls in components, cls
print('PASS: two palette themes, tokens, GTK provider, controls and visual hierarchy')
