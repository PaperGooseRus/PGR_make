"""Source-level guard only; executable GUI is built in Windows/Linux GTK CI."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'gui'/'main.cpp').read_text(encoding='utf-8')
cli=(root/'main.cpp').read_text(encoding='utf-8')
workflow=(root/'.github'/'workflows'/'quality.yml').read_text(encoding='utf-8')
assert 'Скачать необходимые инструменты' in source
assert 'pack_bulk_confirmed' in source
assert 'gtk_window_set_modal(GTK_WINDOW(confirm),TRUE)' in source
assert 'run_pack_cli(ui,{"--pack-install-all"}' in source
assert '"--pack-install-all"' in cli
assert 'manager.install(pkg,{},install_error)' in cli
assert 'tools/catalog.json' in workflow
print('PASS: GTK4 bulk button, confirmation, CLI and Windows catalog packaging present')
