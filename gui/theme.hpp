#ifndef PGR_GUI_THEME_HPP
#define PGR_GUI_THEME_HPP

#include <string>

/**
 * @file theme.hpp
 * @brief Embedded GTK4 Ash & Orange and ChatGPT-inspired light themes.
 *
 * Colors deliberately live outside business logic.  Both themes share the
 * same widget rules; changing a palette does not touch GTK event handlers.
 * CSS is embedded so moving PGR_Make_GUI.exe does not break its appearance.
 */
namespace pgr_gui_theme {

inline constexpr const char* kDarkPalette = R"PGR_CSS(
@define-color pgr_bg #1A1A1A;
@define-color pgr_sidebar #141413;
@define-color pgr_panel #30302E;
@define-color pgr_input #232322;
@define-color pgr_hover #424240;
@define-color pgr_border #424240;
@define-color pgr_text #FAF9F5;
@define-color pgr_muted #B0AEA5;
@define-color pgr_accent #D97757;
@define-color pgr_accent_hover #C6613F;
@define-color pgr_accent_text #141413;
@define-color pgr_success #788C5D;
@define-color pgr_info #6A9BCC;
@define-color pgr_error #E68786;
@define-color pgr_log #141413;
)PGR_CSS";

inline constexpr const char* kLightPalette = R"PGR_CSS(
@define-color pgr_bg #FFFFFF;
@define-color pgr_sidebar #F7F7F8;
@define-color pgr_panel #F4F4F4;
@define-color pgr_input #FFFFFF;
@define-color pgr_hover #EAEAEA;
@define-color pgr_border #E5E5E5;
@define-color pgr_text #0D0D0D;
@define-color pgr_muted #6B6B6B;
@define-color pgr_accent #D97757;
@define-color pgr_accent_hover #C6613F;
@define-color pgr_accent_text #141413;
@define-color pgr_success #506C35;
@define-color pgr_info #396EAD;
@define-color pgr_error #B33B39;
@define-color pgr_log #F7F7F8;
)PGR_CSS";

inline constexpr const char* kComponents = R"PGR_CSS(
/* Ash & Orange · PGR_Make GTK4 design system. */
window, dialog, popover {
    background-color: @pgr_bg;
    color: @pgr_text;
}
label { color: @pgr_text; }
.dim-label, .pgr-subtitle, .pgr-meta { color: @pgr_muted; }
.pgr-app { background-color: @pgr_bg; color: @pgr_text; }
.pgr-header {
    background-color: @pgr_sidebar;
    border-bottom: 1px solid @pgr_border;
    padding: 12px 20px;
}
.pgr-brand { font-weight: 800; font-size: 22px; color: @pgr_text; }
.pgr-brand-mark { font-weight: 800; font-size: 24px; color: @pgr_accent; }
.pgr-subtitle { font-size: 12px; }
.pgr-workspace { padding: 16px; }
.pgr-main { background-color: @pgr_bg; }
.pgr-sidebar {
    background-color: @pgr_sidebar;
    border: 1px solid @pgr_border;
    border-radius: 12px;
    padding: 14px;
}
.pgr-sidebar-heading { font-size: 14px; font-weight: 800; }
.pgr-panel-title { font-size: 14px; font-weight: 700; }
.pgr-section {
    background-color: @pgr_panel;
    border: 1px solid @pgr_border;
    border-radius: 10px;
    padding: 8px 12px;
}
.pgr-section title { font-weight: 650; }
.pgr-toolbar {
    background-color: @pgr_panel;
    border: 1px solid @pgr_border;
    border-radius: 12px;
    padding: 12px;
}
.pgr-statusbar {
    background-color: @pgr_sidebar;
    border-top: 1px solid @pgr_border;
    padding: 8px 18px;
}
.pgr-save-state { font-size: 12px; color: @pgr_success; }
.pgr-save-state.pgr-unsaved { color: @pgr_accent; }
.pgr-pill {
    border: 1px solid @pgr_border;
    background-color: @pgr_panel;
    border-radius: 999px;
    padding: 5px 10px;
    color: @pgr_muted;
    font-size: 11px;
}
.pgr-accent { color: @pgr_accent; }
.pgr-app button {
    color: @pgr_text;
    background-image: none;
    background-color: @pgr_panel;
    border: 1px solid @pgr_border;
    border-radius: 8px;
    padding: 7px 11px;
    box-shadow: none;
}
.pgr-app button:hover { background-color: @pgr_hover; }
.pgr-app button:active { background-color: @pgr_input; }
.pgr-app button:disabled { opacity: 0.5; }
.pgr-app button:focus-visible, .pgr-app entry:focus-within, .pgr-app dropdown:focus-within {
    outline: 2px solid @pgr_accent;
    outline-offset: 1px;
}
.pgr-app button.suggested-action, .pgr-app button.pgr-build {
    color: @pgr_accent_text;
    background-image: none;
    background-color: @pgr_accent;
    border-color: @pgr_accent;
    font-weight: 750;
}
.pgr-app button.suggested-action:hover, .pgr-app button.pgr-build:hover {
    background-color: @pgr_accent_hover;
    border-color: @pgr_accent_hover;
}
.pgr-app button.destructive-action, dialog button.destructive-action {
    background-image: none;
    background-color: @pgr_panel;
    border: 1px solid @pgr_error;
    color: @pgr_error;
}
.pgr-app button.pgr-stop { border-color: @pgr_accent; }
/* Native GtkWindow title controls must not inherit padding and the large
 * touch-target style of regular workspace buttons. GTK4 uses the CSS node
 * `windowcontrols` for minimize/maximize/close buttons. */
headerbar windowcontrols button, windowcontrols button {
    min-width: 24px;
    min-height: 24px;
    padding: 0;
    margin: 0 1px;
    border: 0;
    border-radius: 5px;
    background-image: none;
    background-color: transparent;
    box-shadow: none;
}
windowcontrols button image { -gtk-icon-size: 12px; }
windowcontrols button:hover { background-color: @pgr_hover; }
windowcontrols button.close:hover { background-color: @pgr_error; color: @pgr_accent_text; }
entry, searchentry, dropdown, spin.pgr-app button {
    background-color: @pgr_input;
    background-image: none;
    color: @pgr_text;
    border: 1px solid @pgr_border;
    border-radius: 8px;
    min-height: 32px;
    caret-color: @pgr_accent;
}
entry:focus, searchentry:focus { border-color: @pgr_accent; }
entry placeholder, text placeholder { color: @pgr_muted; }
textview, textview text {
    background-color: @pgr_input;
    color: @pgr_text;
    caret-color: @pgr_accent;
}
textview selection, textview text selection {
    background-color: @pgr_accent;
    color: @pgr_accent_text;
}
.pgr-log, .pgr-log text { background-color: @pgr_log; }
.pgr-log { font-family: monospace; font-size: 12px; }
scrolledwindow.pgr-input-area {
    border: 1px solid @pgr_border;
    border-radius: 8px;
    background-color: @pgr_input;
}
scrolledwindow.pgr-log-area {
    border: 1px solid @pgr_border;
    border-radius: 8px;
}
.pgr-project-list, .pgr-diagnostic-list {
    background-color: transparent;
    color: @pgr_text;
}
.pgr-project-list row, .pgr-diagnostic-list row {
    margin-bottom: 5px;
    border: 1px solid transparent;
    border-radius: 8px;
    background-color: transparent;
}
.pgr-project-list row:hover, .pgr-diagnostic-list row:hover {
    background-color: @pgr_hover;
}
.pgr-project-list row:selected, .pgr-diagnostic-list row:selected {
    background-color: @pgr_panel;
    border-left: 3px solid @pgr_accent;
    border-top: 1px solid @pgr_border;
    border-right: 1px solid @pgr_border;
    border-bottom: 1px solid @pgr_border;
}
.pgr-project-list row:selected label,
.pgr-diagnostic-list row:selected label { color: @pgr_text; }
.pgr-diagnostic-error { border-left: 2px solid @pgr_error; }
.pgr-diagnostic-warning { border-left: 2px solid @pgr_accent; }
.pgr-unavailable { color: @pgr_muted; }
.pgr-output-panel {
    background-color: @pgr_sidebar;
    border: 1px solid @pgr_border;
    border-radius: 10px;
    padding: 8px 12px;
}
.pgr-output-toolbar {
    padding: 2px 3px;
    min-height: 34px;
}
.pgr-output-toolbar searchentry { min-width: 150px; }
.pgr-output-panel notebook { border-radius: 6px; }
.pgr-results {
    border: 1px solid @pgr_border;
    border-radius: 10px;
    background-color: @pgr_panel;
}
notebook { background-color: @pgr_panel; color: @pgr_text; }
notebook header {
    background-color: @pgr_sidebar;
    border-bottom: 1px solid @pgr_border;
}
notebook header tabs tab {
    color: @pgr_muted;
    background-color: transparent;
    border-bottom: 2px solid transparent;
    padding: 7px 13px;
}
notebook header tabs tab:checked {
    color: @pgr_text;
    border-bottom: 2px solid @pgr_accent;
}
notebook header tabs tab:hover { background-color: @pgr_hover; }
checkbutton { color: @pgr_text; }
checkbutton check:checked { background-color: @pgr_accent; color: @pgr_accent_text; }
.pgr-app separator { background-color: @pgr_border; }
.pgr-app paned > separator {
    background-color: @pgr_border;
    min-width: 5px;
    min-height: 5px;
    border-radius: 2px;
}
.pgr-app paned > separator:hover { background-color: @pgr_accent; }
.pgr-app paned.pgr-vertical-paned > separator { min-height: 7px; }
)PGR_CSS";

/** @brief Produce the complete stylesheet for a chosen appearance. */
inline std::string stylesheet(bool dark) {
    return std::string(dark ? kDarkPalette : kLightPalette) + kComponents;
}

} // namespace pgr_gui_theme
#endif // PGR_GUI_THEME_HPP
