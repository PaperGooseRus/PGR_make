/**
 * PGR_Make GTK4 frontend. Build operations are performed by PGR_Make CLI.
 * One project model, one config.cfg, and no duplicated compiler backend.
 * Layout: GtkPaned, editor / commands at left, project list at right.
 */
#include <gtk/gtk.h>
#ifndef PGR_APP_VERSION
#define PGR_APP_VERSION "dev"
#endif
#include "theme.hpp"
#include "layout_settings.hpp"
#include "ux_model.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <thread>
#include <atomic>
#include <cctype>
#include <ctime>
#include <chrono>
#include <cstring>
#include "../core/diagnostics.hpp"
#include "../core/build_controller.hpp"
#include "../core/toolchain_manager.hpp"
#include "../core/pack_manager.hpp"
#include "../core/pack_bulk.hpp"
#include "../core/make.hpp"
#include "../core/pack_tools.hpp"
#include "../core/project_setup.hpp"
#include "../core/workspace.hpp"

namespace fs = std::filesystem;
using pgr_workspace::project_t;
using pgr_workspace::registry_t;

struct Choice {
    GtkWidget* dropdown = nullptr;
    GtkWidget* custom = nullptr;
    std::vector<std::string> values; // Does not include the final custom entry
};
struct Gui {
    explicit Gui(fs::path root) : registry(std::move(root)) {
        catalog.load_builtin();
        layout = pgr_gui_layout::read(registry.projects_root() / ".pgr_gui_layout");
        std::ifstream stored_theme(registry.projects_root() / ".pgr_gui_theme");
        std::string value;
        if (stored_theme && std::getline(stored_theme, value)) dark_theme = value != "light";
    }
    registry_t registry;
    flag_catalog_t catalog;
    std::vector<std::string> installed_tool_ids(const std::string& type) {
        std::vector<std::string> values={""};
        pgr_pack_manager::manager manager(registry.pack_root(),registry.root());
        std::string error;
        if(manager.load(error))for(const auto& item:manager.entries())
            if(item.tool==type || (type=="compiler" && (item.tool=="gcc"||item.tool=="clang"||item.tool=="msvc")))
                values.push_back(item.id);
        return values;
    }
    std::vector<project_t> projects;
    project_t selected{};
    bool refreshing = false;
    bool loading = false;
    bool dirty = false;
    bool close_dialog_open = false;
    pgr_gui_ux::build_mode running_mode = pgr_gui_ux::build_mode::generated;
    std::chrono::system_clock::time_point build_started{};
    fs::path last_artifact;
    fs::path output_directory;
    fs::file_time_type artifact_timestamp_before = fs::file_time_type::min();
    bool busy = false;
    bool operation_is_build = false;
    bool dark_theme = true;
    pgr_gui_layout::settings layout;
    bool restoring_layout = true;
    guint layout_save_source = 0;
    int regular_split = 565;
    bool output_expanded = false;
    GtkWidget *vertical_paned = nullptr, *horizontal_paned = nullptr;
    GtkWidget *output_panel = nullptr, *output_toggle = nullptr, *output_expand = nullptr;
    GtkWidget *log_search = nullptr, *search_status = nullptr, *autoscroll_button = nullptr;
    GtkWidget *settings_search = nullptr, *category_select = nullptr;
    GtkTextTag* search_tag = nullptr;
    GtkCssProvider* theme_provider = nullptr;
    GtkWidget* theme_button = nullptr;
    GtkWidget* active_project_label = nullptr;
    GtkWidget *summary_compiler = nullptr, *summary_mode = nullptr, *summary_profile = nullptr;
    GtkWidget *summary_cmake = nullptr, *install_missing_button = nullptr;
    std::string missing_pin;
    GtkWidget *validation_label = nullptr, *validation_action = nullptr;
    GtkWidget *result_status = nullptr, *result_detail = nullptr;
    GtkWidget *open_folder_button = nullptr, *run_artifact_button = nullptr;
    GtkWidget *history_box = nullptr;
    GtkWidget* status_spinner = nullptr;
    std::thread worker;
    pgr_process::cancel_token cancellation;
    std::vector<bool> project_available;
    GtkWidget* project_search = nullptr;
    GtkWidget* settings_form = nullptr;
    GtkWidget* build_choice = nullptr;
    GtkWidget* dirty_indicator = nullptr;
    GtkWidget *severity_errors = nullptr, *severity_warnings = nullptr;

    GtkWidget *window = nullptr, *project_list = nullptr, *project_path = nullptr;
    GtkWidget *status = nullptr, *left_controls = nullptr, *stop_button = nullptr;
    GtkWidget *log_view = nullptr, *diagnostics_box = nullptr, *diags_count = nullptr;
    GtkWidget *results = nullptr, *profile = nullptr, *build_mode = nullptr;
    GtkWidget *compiler = nullptr, *compiler_type = nullptr;
    GtkTextBuffer* log_buffer = nullptr;
    std::map<std::string, Choice> singles;
    std::map<std::string, std::vector<std::pair<std::string, GtkWidget*>>> multis;
    std::map<std::string, GtkWidget*> fields;
    std::map<std::string, GtkWidget*> lists;
    std::vector<GtkWidget*> project_actions;
    std::vector<GtkWidget*> quick_actions;
};
static std::string path_utf8(const fs::path& p) { return pgr_workspace::utf8(p); }
static void set_saved_state(Gui* ui, bool dirty) {
    if (!ui->dirty_indicator) return;
    gtk_label_set_text(GTK_LABEL(ui->dirty_indicator), dirty ? "● Изменено" : "✓ Сохранено");
    if (dirty) gtk_widget_add_css_class(ui->dirty_indicator, "pgr-unsaved");
    else gtk_widget_remove_css_class(ui->dirty_indicator, "pgr-unsaved");
}
static void apply_theme(Gui* ui) {
    if (!ui->theme_provider) return;
    const std::string css = pgr_gui_theme::stylesheet(ui->dark_theme);
    // GTK 4.10 compatibility: load_from_string() was introduced in GTK 4.12.
    gtk_css_provider_load_from_data(ui->theme_provider, css.c_str(), -1);
    if (GtkSettings* settings = gtk_settings_get_default())
        g_object_set(settings, "gtk-application-prefer-dark-theme", ui->dark_theme ? TRUE : FALSE, nullptr);
    if (ui->theme_button)
        gtk_button_set_label(GTK_BUTTON(ui->theme_button), ui->dark_theme ? "☀ Светлая" : "◐ Тёмная");
}
static void toggle_theme(GtkButton*, gpointer data) {
    auto* ui = static_cast<Gui*>(data);
    ui->dark_theme = !ui->dark_theme;
    apply_theme(ui);
    // Persist appearance independently of all per-project compiler settings.
    std::ofstream stored_theme(ui->registry.projects_root() / ".pgr_gui_theme", std::ios::trunc);
    if (stored_theme) stored_theme << (ui->dark_theme ? "dark\n" : "light\n");
}
static void set_status(Gui* ui, const std::string& value) {
    gtk_label_set_text(GTK_LABEL(ui->status), value.c_str());
}
static GtkWidget* label(const char* text) {
    GtkWidget* w = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(w), 0);
    return w;
}
static GtkWidget* row(int gap = 8) { return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, gap); }
static GtkWidget* column(int gap = 8) { return gtk_box_new(GTK_ORIENTATION_VERTICAL, gap); }
static void margins(GtkWidget* w, int m = 12) {
    gtk_widget_set_margin_start(w, m); gtk_widget_set_margin_end(w, m);
    gtk_widget_set_margin_top(w, m); gtk_widget_set_margin_bottom(w, m);
}
static void log_line(Gui* ui, const std::string& s) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(ui->log_buffer, &end);
    gtk_text_buffer_insert(ui->log_buffer, &end, (s + "\n").c_str(), -1);
    const int count = gtk_text_buffer_get_char_count(ui->log_buffer);
    if (count > 140000) {
        GtkTextIter a, b;
        gtk_text_buffer_get_start_iter(ui->log_buffer, &a);
        gtk_text_buffer_get_iter_at_offset(ui->log_buffer, &b, count - 100000);
        gtk_text_buffer_delete(ui->log_buffer, &a, &b);
    }
    gtk_text_buffer_get_end_iter(ui->log_buffer, &end);
    if (ui->layout.autoscroll)
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(ui->log_view), &end, 0., FALSE, 0., 1.);
}
static void set_busy(Gui* ui, bool busy) {
    gtk_widget_set_sensitive(ui->left_controls, !busy && !ui->selected.name.empty());
    gtk_widget_set_sensitive(ui->project_list, !busy);
    if (ui->build_choice) gtk_widget_set_sensitive(ui->build_choice,!busy);
    for (auto* button : ui->project_actions) gtk_widget_set_sensitive(button, !busy);
    for (auto* button : ui->quick_actions)
        gtk_widget_set_sensitive(button, !busy && !ui->selected.name.empty());
    gtk_widget_set_sensitive(ui->stop_button, busy);
    if (ui->status_spinner) {
        gtk_widget_set_visible(ui->status_spinner, busy);
        if (busy) gtk_spinner_start(GTK_SPINNER(ui->status_spinner));
        else gtk_spinner_stop(GTK_SPINNER(ui->status_spinner));
    }
    ui->busy = busy;
}
static GtkWidget* action(Gui* ui, GtkWidget* container,
                         const char* title, const char* id);
static void show_diagnostics(Gui* ui);
static void show_history(Gui* ui);
static void refresh_project_summary(Gui* ui);
static bool check_configuration(Gui* ui, bool focus_error);
static bool save_settings(Gui* ui);
static void load_project(Gui* ui, const project_t& project);
static void refresh_projects(Gui* ui, const std::string& preferred = {});
// Restore/save the two splitter positions and output behavior independently of the project.
static void save_layout_now(Gui* ui) {
    if (ui->restoring_layout || !ui->window) return;
    const int width = gtk_widget_get_width(ui->window);
    const int height = gtk_widget_get_height(ui->window);
    if (width >= 940) ui->layout.window_width = std::min(width, 3840);
    if (height >= 620) ui->layout.window_height = std::min(height, 2160);
    if (ui->horizontal_paned) {
        const int position = gtk_paned_get_position(GTK_PANED(ui->horizontal_paned));
        if (position >= 600) {
            ui->layout.projects_split = position;
            const int available = gtk_widget_get_width(ui->horizontal_paned);
            if (available >= 900) ui->layout.projects_permille = std::clamp(position * 1000 / available, 450, 850);
        }
    }
    if (ui->vertical_paned && ui->layout.output_visible && !ui->output_expanded) {
        const int position = gtk_paned_get_position(GTK_PANED(ui->vertical_paned));
        if (position >= 280) {
            ui->layout.output_split = position;
            const int available = gtk_widget_get_height(ui->vertical_paned);
            if (available >= 550) ui->layout.output_permille = std::clamp(position * 1000 / available, 350, 850);
        }
    }
    if (ui->results) ui->layout.output_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(ui->results));
    if (ui->category_select)
        ui->layout.category = static_cast<int>(gtk_drop_down_get_selected(GTK_DROP_DOWN(ui->category_select)));
    pgr_gui_layout::write(ui->registry.projects_root() / ".pgr_gui_layout", ui->layout);
}
static gboolean save_layout_timeout(gpointer payload) {
    auto* ui = static_cast<Gui*>(payload);
    ui->layout_save_source = 0;
    save_layout_now(ui);
    return G_SOURCE_REMOVE;
}
static void schedule_layout_save(Gui* ui) {
    if (ui->restoring_layout) return;
    if (ui->layout_save_source) g_source_remove(ui->layout_save_source);
    ui->layout_save_source = g_timeout_add(450, save_layout_timeout, ui);
}
static void on_split_position(GObject*, GParamSpec*, gpointer payload) {
    schedule_layout_save(static_cast<Gui*>(payload));
}
// GTK allocates the panes after presentation. Restore proportions using the
// actual allocated size, never an old absolute pixel position on a new monitor.
static gboolean restore_split_ratios(gpointer payload) {
    auto* ui = static_cast<Gui*>(payload);
    const int w = gtk_widget_get_width(ui->horizontal_paned);
    const int h = gtk_widget_get_height(ui->vertical_paned);
    if (w > 900) {
        int position = std::clamp(w * ui->layout.projects_permille / 1000, 620, w - 280);
        gtk_paned_set_position(GTK_PANED(ui->horizontal_paned), position);
    }
    if (h > 560) {
        int position = std::clamp(h * ui->layout.output_permille / 1000, 285, h - 145);
        gtk_paned_set_position(GTK_PANED(ui->vertical_paned), position);
    }
    ui->restoring_layout = false;
    return G_SOURCE_REMOVE;
}
static void on_output_page(GtkNotebook*, GtkWidget*, guint, gpointer payload) {
    schedule_layout_save(static_cast<Gui*>(payload));
}
static void toggle_output(GtkButton*, gpointer payload) {
    auto* ui = static_cast<Gui*>(payload);
    if (!ui->output_panel) return;
    if (ui->layout.output_visible && ui->output_expanded) {
        ui->output_expanded = false;
        if (ui->output_expand)
            gtk_button_set_label(GTK_BUTTON(ui->output_expand), "Развернуть");
    }
    ui->layout.output_visible = !ui->layout.output_visible;
    gtk_widget_set_visible(ui->output_panel, ui->layout.output_visible);
    if (ui->output_toggle)
        gtk_button_set_label(GTK_BUTTON(ui->output_toggle),
                             ui->layout.output_visible ? "▾ Вывод" : "▸ Вывод");
    if (ui->layout.output_visible && ui->vertical_paned)
        gtk_paned_set_position(GTK_PANED(ui->vertical_paned), ui->layout.output_split);
    schedule_layout_save(ui);
}
static void expand_output(GtkButton*, gpointer payload) {
    auto* ui = static_cast<Gui*>(payload);
    if (!ui->vertical_paned) return;
    if (!ui->layout.output_visible) toggle_output(nullptr, ui);
    if (ui->output_expanded) {
        ui->output_expanded = false;
        gtk_paned_set_position(GTK_PANED(ui->vertical_paned), ui->regular_split);
    } else {
        ui->regular_split = gtk_paned_get_position(GTK_PANED(ui->vertical_paned));
        if (ui->regular_split < 280) ui->regular_split = ui->layout.output_split;
        ui->output_expanded = true;
        gtk_paned_set_position(GTK_PANED(ui->vertical_paned), 280);
    }
    gtk_button_set_label(GTK_BUTTON(ui->output_expand),
                         ui->output_expanded ? "Восстановить" : "Развернуть");
    schedule_layout_save(ui);
}
static void on_autoscroll(GtkCheckButton* check, gpointer payload) {
    auto* ui = static_cast<Gui*>(payload);
    ui->layout.autoscroll = gtk_check_button_get_active(check);
    schedule_layout_save(ui);
}
static void on_log_search(GtkEditable* editable, gpointer payload) {
    auto* ui = static_cast<Gui*>(payload);
    if (!ui->log_buffer || !ui->search_tag) return;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(ui->log_buffer, &start, &end);
    gtk_text_buffer_remove_tag(ui->log_buffer, ui->search_tag, &start, &end);
    const char* phrase = gtk_editable_get_text(editable);
    if (!phrase || !*phrase) {
        if (ui->search_status) gtk_label_set_text(GTK_LABEL(ui->search_status), "");
        return;
    }
    int matches = 0;
    GtkTextIter current = start, first, last;
    while (matches < 300 && gtk_text_iter_forward_search(&current, phrase,
           GTK_TEXT_SEARCH_CASE_INSENSITIVE, &first, &last, nullptr)) {
        gtk_text_buffer_apply_tag(ui->log_buffer, ui->search_tag, &first, &last);
        if (matches == 0) gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(ui->log_view), &first, 0., FALSE, 0., 0.);
        current = last;
        ++matches;
    }
    if (ui->search_status) {
        const std::string count = std::to_string(matches) + (matches == 300 ? "+ совпадений" : " совпадений");
        gtk_label_set_text(GTK_LABEL(ui->search_status), count.c_str());
    }
}
static void clear_log(GtkButton*, gpointer payload) {
    auto* ui = static_cast<Gui*>(payload);
    gtk_text_buffer_set_text(ui->log_buffer, "", -1);
    if (ui->search_status) gtk_label_set_text(GTK_LABEL(ui->search_status), "");
}
static void copy_log(GtkButton*, gpointer payload) {
    auto* ui = static_cast<Gui*>(payload);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(ui->log_buffer, &start, &end);
    gchar* content = gtk_text_buffer_get_text(ui->log_buffer, &start, &end, FALSE);
    GdkClipboard* clipboard = gtk_widget_get_clipboard(ui->window);
    gdk_clipboard_set_text(clipboard, content ? content : "");
    g_free(content);
    set_status(ui, "Журнал скопирован в буфер обмена");
}
static void primary_build(GtkButton*, gpointer);
static gboolean on_global_key(GtkEventControllerKey*, guint keyval, guint,
                              GdkModifierType modifiers, gpointer payload) {
    auto* ui = static_cast<Gui*>(payload);
    if (!(modifiers & GDK_CONTROL_MASK)) return FALSE;
    switch (gdk_keyval_to_lower(keyval)) {
        case GDK_KEY_b: primary_build(nullptr, ui); return TRUE;
        case GDK_KEY_s: if (!ui->busy) save_settings(ui); return TRUE;
        case GDK_KEY_j: toggle_output(nullptr, ui); return TRUE;
        case GDK_KEY_f:
            if (!ui->layout.output_visible) toggle_output(nullptr, ui);
            gtk_notebook_set_current_page(GTK_NOTEBOOK(ui->results), 1);
            gtk_widget_grab_focus(ui->log_search);
            return TRUE;
        default: return FALSE;
    }
}

static void mark_dirty(Gui* ui) {
    if (ui->loading || ui->selected.name.empty()) return;
    ui->dirty = true;
    set_saved_state(ui, true);
    refresh_project_summary(ui);
}
static void changed_text(GtkEditable*, gpointer data) { mark_dirty(static_cast<Gui*>(data)); }
static void changed_buffer(GtkTextBuffer*, gpointer data) { mark_dirty(static_cast<Gui*>(data)); }
static void changed_selection(GObject*, GParamSpec*, gpointer data) { mark_dirty(static_cast<Gui*>(data)); }
static void changed_check(GtkCheckButton*, gpointer data) { mark_dirty(static_cast<Gui*>(data)); }
static void observe_changes(Gui* ui) {
    for (auto& item : ui->fields)
        g_signal_connect(item.second,"changed",G_CALLBACK(changed_text),ui);
    for (auto& item : ui->singles) {
        g_signal_connect(item.second.dropdown,"notify::selected",G_CALLBACK(changed_selection),ui);
        g_signal_connect(item.second.custom,"changed",G_CALLBACK(changed_text),ui);
    }
    for (auto& item : ui->lists)
        g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(item.second)),"changed",G_CALLBACK(changed_buffer),ui);
    for (auto& item : ui->multis) for (auto& flag : item.second)
        g_signal_connect(flag.second,"toggled",G_CALLBACK(changed_check),ui);
}


// Lists are intentionally multiline, one source/include/library/package per line.
static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        const size_t last = line.find_last_not_of(" \t");
        out.push_back(line.substr(first, last - first + 1));
    }
    return out;
}
static std::string join_lines(const std::vector<std::string>& values) {
    std::string s;
    for (const auto& v : values) { if (!s.empty()) s += '\n'; s += v; }
    return s;
}
static std::string get_entry(GtkWidget* w) {
    return w ? gtk_editable_get_text(GTK_EDITABLE(w)) : "";
}
static void set_entry(GtkWidget* w, const std::string& s) {
    if (w) gtk_editable_set_text(GTK_EDITABLE(w), s.c_str());
}
static std::string text_contents(GtkWidget* view) {
    GtkTextBuffer* b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter begin, end;
    gtk_text_buffer_get_bounds(b, &begin, &end);
    gchar* s = gtk_text_buffer_get_text(b, &begin, &end, FALSE);
    std::string value = s ? s : "";
    g_free(s);
    return value;
}
static void set_text(GtkWidget* view, const std::string& s) {
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), s.c_str(), -1);
}
static std::string choice_value(const Choice& c) {
    const guint index = gtk_drop_down_get_selected(GTK_DROP_DOWN(c.dropdown));
    return index < c.values.size() ? c.values[index] : get_entry(c.custom);
}
static void choice_set(Choice& c, const std::string& value) {
    for (size_t i = 0; i < c.values.size(); ++i) {
        if (value == c.values[i]) { gtk_drop_down_set_selected(GTK_DROP_DOWN(c.dropdown), static_cast<guint>(i)); return; }
    }
    set_entry(c.custom, value);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(c.dropdown), static_cast<guint>(c.values.size()));
}
static void on_choice_changed(GObject* object, GParamSpec*, gpointer data) {
    auto* custom = GTK_WIDGET(data);
    auto* dd = GTK_DROP_DOWN(object);
    // The user can supply flags absent from the bundled catalog.
    const guint selected = gtk_drop_down_get_selected(dd);
    const guint custom_index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(dd), "custom-index"));
    gtk_widget_set_visible(custom, selected == custom_index);
}
static void add_choice(Gui* ui, GtkWidget* parent, const char* key,
                       const char* caption, const std::vector<std::string>& values,
                       const std::vector<std::string>& explanations = {}) {
    GtkWidget* container = column(4);
    gtk_box_append(GTK_BOX(container), label(caption));
    GtkWidget* horizontal = row();
    std::vector<std::string> options;
    options.reserve(values.size() + 1);
    for (size_t i = 0; i < values.size(); ++i) {
        std::string text = values[i].empty() ? "(не задано)" : values[i];
        if (i < explanations.size() && !explanations[i].empty()) text += " · " + explanations[i];
        options.push_back(std::move(text));
    }
    options.push_back("Другое значение…");
    std::vector<const char*> strings;
    for (const auto& s : options) strings.push_back(s.c_str());
    strings.push_back(nullptr);
    GtkWidget* dd = gtk_drop_down_new_from_strings(strings.data());
    gtk_widget_set_hexpand(dd, TRUE);
    GtkWidget* custom = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(custom), "Пользовательское значение");
    gtk_widget_set_hexpand(custom, TRUE);
    gtk_widget_set_visible(custom, FALSE);
    g_object_set_data(G_OBJECT(dd), "custom-index", GUINT_TO_POINTER(static_cast<guint>(values.size())));
    g_signal_connect(dd, "notify::selected", G_CALLBACK(on_choice_changed), custom);
    gtk_box_append(GTK_BOX(horizontal), dd);
    gtk_box_append(GTK_BOX(horizontal), custom);
    gtk_box_append(GTK_BOX(container), horizontal);
    gtk_box_append(GTK_BOX(parent), container);
    ui->singles[key] = {dd, custom, values};
}
static void add_multi(Gui* ui, GtkWidget* parent, const char* key,
                      const char* caption, const flag_group_t* group) {
    GtkWidget* exp = gtk_expander_new(caption);
    GtkWidget* wrap = column(3);
    margins(wrap, 8);
    if (group) {
        for (const auto& item : group->entries) {
            GtkWidget* cb = gtk_check_button_new_with_label(item.flag.c_str());
            gtk_widget_set_tooltip_text(cb, item.description.c_str());
            gtk_box_append(GTK_BOX(wrap), cb);
            ui->multis[key].emplace_back(item.flag, cb);
        }
    }
    GtkWidget* custom_label = label("Другие флаги (по одному на строку):");
    gtk_box_append(GTK_BOX(wrap), custom_label);
    GtkWidget* custom = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(custom), GTK_WRAP_NONE);
    gtk_widget_set_size_request(custom, -1, 58);
    gtk_widget_add_css_class(custom, "pgr-input");
    gtk_box_append(GTK_BOX(wrap), custom);
    ui->lists[std::string("extra_") + key] = custom;
    gtk_expander_set_child(GTK_EXPANDER(exp), wrap);
    gtk_box_append(GTK_BOX(parent), exp);
}
static std::vector<std::string> read_multi(Gui* ui, const std::string& key) {
    std::vector<std::string> out;
    for (auto& [value, widget] : ui->multis[key])
        if (gtk_check_button_get_active(GTK_CHECK_BUTTON(widget))) out.push_back(value);
    for (const auto& value : split_lines(text_contents(ui->lists["extra_" + key])))
        if (std::find(out.begin(), out.end(), value) == out.end()) out.push_back(value);
    return out;
}
static void load_multi(Gui* ui, const std::string& key, const std::vector<std::string>& v) {
    std::vector<std::string> extra;
    for (const auto& flag : v) {
        bool known = false;
        for (const auto& item : ui->multis[key]) if (item.first == flag) known = true;
        if (!known) extra.push_back(flag);
    }
    for (auto& [value, widget] : ui->multis[key])
        gtk_check_button_set_active(GTK_CHECK_BUTTON(widget),
            std::find(v.begin(), v.end(), value) != v.end());
    set_text(ui->lists["extra_" + key], join_lines(extra));
}

// GTK's native file chooser is supported by GTK4/Windows. The response handler
// owns the callback state; no dangling pointer to a transient dialog is kept.
struct BrowseState { Gui* ui; GtkWidget* target; bool multiline; std::string kind; };
static void browse_finished(GObject* object, GAsyncResult* result, gpointer payload) {
    std::unique_ptr<BrowseState> state(static_cast<BrowseState*>(payload));
    GtkFileDialog* dialog = GTK_FILE_DIALOG(object);
    GError* error = nullptr;
    const bool folder = state->kind == "folder" || state->kind == "package_dir";
    GFile* file = folder ? gtk_file_dialog_select_folder_finish(dialog, result, &error)
                         : gtk_file_dialog_open_finish(dialog, result, &error);
    // Cancelled dialogs have no effect on the configuration.
    g_clear_error(&error);
    if (file) {
        gchar* path = g_file_get_path(file);
        if (path) {
            std::string value(path);
            if (state->kind == "package") {
                fs::path p = pgr_workspace::from_utf8(value);
                std::string name = p.filename().u8string();
                const auto cut = name.find("Config.cmake");
                const auto low = name.find("-config.cmake");
                if (cut != std::string::npos) name = name.substr(0, cut);
                else if (low != std::string::npos) name = name.substr(0, low);
                if (cut == std::string::npos && low == std::string::npos) {
                    set_status(state->ui, "Выберите FooConfig.cmake или foo-config.cmake");
                } else if (!name.empty()) {
                    auto packages = split_lines(text_contents(state->target));
                    if (std::find(packages.begin(), packages.end(), name) == packages.end()) packages.push_back(name);
                    set_text(state->target, join_lines(packages));
                    auto dirs = split_lines(text_contents(state->ui->lists["package_dirs"]));
                    const std::string location = path_utf8(p.parent_path());
                    if (std::find(dirs.begin(), dirs.end(), location) == dirs.end()) dirs.push_back(location);
                    set_text(state->ui->lists["package_dirs"], join_lines(dirs));
                }
            } else if (state->multiline) {
                if (state->kind == "sources" && !state->ui->selected.name.empty()) {
                    auto relative = pgr_workspace::from_utf8(value).lexically_relative(state->ui->selected.source_root);
                    if (!relative.empty() && relative.generic_u8string().rfind("..", 0) != 0)
                        value = relative.generic_u8string();
                }
                auto all = split_lines(text_contents(state->target));
                if (std::find(all.begin(), all.end(), value) == all.end()) all.push_back(value);
                set_text(state->target, join_lines(all));
            } else set_entry(state->target, value);
            g_free(path);
        }
        g_object_unref(file);
    }
    g_object_unref(state->target);
}
static void browse(GtkButton* button, gpointer data) {
    auto* ui = static_cast<Gui*>(data);
    const char* field = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "field"));
    const char* kind = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "kind"));
    if (!field || !kind) return;
    auto it = ui->fields.find(field);
    auto lit = ui->lists.find(field);
    GtkWidget* target = it != ui->fields.end() ? it->second : (lit != ui->lists.end() ? lit->second : nullptr);
    if (!target) return;
    const std::string type(kind);
    const bool folder = type == "folder" || type == "package_dir";
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, folder ? "Выберите каталог" : "Выберите файл");
    auto* state = new BrowseState{ui, GTK_WIDGET(g_object_ref(target)), lit != ui->lists.end(), type};
    if (folder) gtk_file_dialog_select_folder(dialog, GTK_WINDOW(ui->window), nullptr, browse_finished, state);
    else gtk_file_dialog_open(dialog, GTK_WINDOW(ui->window), nullptr, browse_finished, state);
    g_object_unref(dialog);
}
static GtkWidget* path_field(Gui* ui, GtkWidget* parent, const char* key, const char* caption,
                             const char* browse_kind = nullptr) {
    GtkWidget* wrap = column(4);
    gtk_box_append(GTK_BOX(wrap), label(caption));
    GtkWidget* h = row();
    GtkWidget* entry = gtk_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);
    ui->fields[key] = entry;
    gtk_box_append(GTK_BOX(h), entry);
    if (browse_kind) {
        GtkWidget* b = gtk_button_new_with_label("Обзор…");
        g_object_set_data_full(G_OBJECT(b), "field", g_strdup(key), g_free);
        g_object_set_data_full(G_OBJECT(b), "kind", g_strdup(browse_kind), g_free);
        g_signal_connect(b, "clicked", G_CALLBACK(browse), ui);
        gtk_box_append(GTK_BOX(h), b);
    }
    gtk_box_append(GTK_BOX(wrap), h);
    gtk_box_append(GTK_BOX(parent), wrap);
    return entry;
}
static void list_field(Gui* ui, GtkWidget* parent, const char* key, const char* caption,
                       const char* browse_kind = nullptr) {
    GtkWidget* wrap = column(4);
    gtk_box_append(GTK_BOX(wrap), label(caption));
    GtkWidget* scroller = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scroller, -1, 70);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    GtkWidget* view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_NONE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), view);
    gtk_widget_add_css_class(scroller, "pgr-input-area");
    ui->lists[key] = view;
    gtk_box_append(GTK_BOX(wrap), scroller);
    if (browse_kind) {
        GtkWidget* b = gtk_button_new_with_label("+ Добавить через проводник…");
        g_object_set_data_full(G_OBJECT(b), "field", g_strdup(key), g_free);
        g_object_set_data_full(G_OBJECT(b), "kind", g_strdup(browse_kind), g_free);
        g_signal_connect(b, "clicked", G_CALLBACK(browse), ui);
        gtk_box_append(GTK_BOX(wrap), b);
    }
    gtk_box_append(GTK_BOX(parent), wrap);
}
static GtkWidget* section(GtkWidget* parent, const char* title) {
    GtkWidget* exp = gtk_expander_new(title);
    gtk_widget_add_css_class(exp, "pgr-section");
    // Keep the first, most-used category immediately accessible.
    gtk_expander_set_expanded(GTK_EXPANDER(exp), g_str_has_prefix(title, "Компилятор") ? TRUE : FALSE);
    GtkWidget* inside = column(9);
    margins(inside, 10);
    gtk_expander_set_child(GTK_EXPANDER(exp), inside);
    gtk_box_append(GTK_BOX(parent), exp);
    return inside;
}
static void add_group_choice(Gui* ui, GtkWidget* box, const char* key, const char* caption) {
    std::vector<std::string> values = {""}, desc = {""};
    if (auto* group = ui->catalog.find(key))
        for (const auto& entry : group->entries) { values.push_back(entry.flag); desc.push_back(entry.description); }
    add_choice(ui, box, key, caption, values, desc);
}
static void build_configuration_form(Gui* ui, GtkWidget* host) {
    GtkWidget* general = section(host, "Компилятор и язык");
    path_field(ui, general, "compiler", "Компилятор (имя или путь)", "file");
    action(ui, general, "Выбрать компилятор из Pack", "pack");
    add_group_choice(ui, general, "standard", "Стандарт C++");
    add_group_choice(ui, general, "optimization", "Оптимизация");
    add_group_choice(ui, general, "debug", "Отладочная информация");
    add_choice(ui, general, "c_standard", "Стандарт C", {"", "-std=c99", "-std=c11", "-std=c17", "-std=c23"});
    GtkWidget* versions = section(host, "Установленные инструменты и версии");
    gtk_box_append(GTK_BOX(versions),label("Закреплённые версии запускаются по абсолютным путям. Пустое значение = автоматический поиск."));
    add_choice(ui,versions,"compiler_id","Компилятор (GCC/Clang/MSVC)",ui->installed_tool_ids("compiler"));
    add_choice(ui,versions,"cmake_id","Версия CMake",ui->installed_tool_ids("cmake"));
    add_choice(ui,versions,"ninja_id","Версия Ninja",ui->installed_tool_ids("ninja"));
    add_choice(ui,versions,"objcopy_id","Версия objcopy",ui->installed_tool_ids("objcopy"));
    action(ui,versions,"Открыть менеджер Pack…","pack-manager");

    GtkWidget* flags = section(host, "Флаги и оптимизация");
    add_multi(ui, flags, "warnings", "Предупреждения", ui->catalog.find("warnings"));
    add_multi(ui, flags, "sanitizers", "Санитайзеры", ui->catalog.find("sanitizers"));
    add_group_choice(ui, flags, "lto", "LTO");
    add_group_choice(ui, flags, "arch", "Архитектура");
    add_multi(ui, flags, "specific_gcc", "Дополнительные GCC-флаги", ui->catalog.find("specific_gcc"));
    add_multi(ui, flags, "specific_clang", "Дополнительные Clang-флаги", ui->catalog.find("specific_clang"));

    GtkWidget* files = section(host, "Исходники, каталоги и библиотеки");
    list_field(ui, files, "source_files", "Исходники (по одному на строку)", "sources");
    path_field(ui, files, "output_file", "Выходной файл относительно каталога проекта");
    list_field(ui, files, "include_dirs", "Include-каталоги (по одному на строку)", "folder");
    list_field(ui, files, "library_dirs", "Каталоги библиотек", "folder");
    list_field(ui, files, "libraries", "Библиотеки", "file");

    GtkWidget* cmake = section(host, "CMake, зависимости и профили");
    list_field(ui, cmake, "packages", "Пакеты find_package (Foo или Foo|Foo::Target)", "package");
    list_field(ui, cmake, "package_dirs", "Каталоги поиска CMake-пакетов (CMAKE_PREFIX_PATH)", "folder");
    add_choice(ui, cmake, "profile", "Профиль", {"", "debug", "release"});
    action(ui, cmake, "Сохранить профиль…", "profile-save");
    action(ui, cmake, "Загрузить профиль…", "profile-load");
    path_field(ui, cmake, "toolchain_file", "CMake toolchain file", "file");
    path_field(ui, cmake, "linker_script", "Linker script (.ld)", "file");
    add_choice(ui, cmake, "target_type", "Тип библиотеки", {"", "static", "shared", "object", "interface"});
    path_field(ui, cmake, "toolchain_prefix", "Префикс кросс-компилятора");
    add_choice(ui, cmake, "package_manager", "Пакетный менеджер", {"none", "vcpkg", "conan"});
    path_field(ui, cmake, "objcopy", "Программа objcopy", "file");

    GtkWidget* actions = section(host, "Дополнительные команды");
    const std::vector<std::pair<const char*, const char*>> commands = {
        {"Показать команду компиляции", "command"},
        {"Собрать напрямую", "direct"},
        {"Сгенерировать CMakeLists.txt", "generate"},
        {"Собрать через PGR CMake", "build"},
        {"Сохранить конфигурацию", "save"},
        {"Загрузить конфигурацию", "reload"},
        {"Очистить каталог сборки", "clean"},
        {"Экспортировать compile_commands.json", "export"},
        {"Выбрать другой проект (справа)", "switch"},
        {"Собрать существующий CMakeLists.txt", "native"},
        {"Показать диагностику", "diagnostics"},
        {"Проверить clangd", "analyze"},
        {"Создать CMakePresets.json", "presets"},
        {"Выход", "exit"}
    };
    for (const auto& [caption, cmd] : commands) action(ui, actions, caption, cmd);
}

static bool save_settings(Gui* ui) {
    if (ui->selected.name.empty()) { set_status(ui, "Сначала выберите проект"); return false; }
    make_t build;
    if (build.load(path_utf8(ui->selected.config())) != 0) {
        set_status(ui, "Не удалось загрузить config.cfg"); return false;
    }
    const auto field = [ui](const char* key) { return get_entry(ui->fields.at(key)); };
    const auto lines = [ui](const char* key) { return split_lines(text_contents(ui->lists.at(key))); };
    const auto pick = [ui](const char* key) { return choice_value(ui->singles.at(key)); };
    build.set_compiler(field("compiler"));
    build.set_pins(pick("compiler_id"),pick("cmake_id"),pick("ninja_id"),pick("objcopy_id"));
    build.set_standard(pick("standard"));
    build.set_optimization(pick("optimization"));
    build.set_debug(pick("debug"));
    build.set_lto(pick("lto"));
    build.set_arch(pick("arch"));
    build.set_c_standard(pick("c_standard"));
    build.set_target_type(pick("target_type"));
    build.set_profile(pick("profile"));
    build.set_package_manager(pick("package_manager"));
    build.set_output_file(field("output_file"));
    build.set_toolchain_file(field("toolchain_file"));
    build.set_linker_script(field("linker_script"));
    build.set_toolchain_prefix(field("toolchain_prefix"));
    build.set_objcopy(field("objcopy"));
    build.set_source_files(lines("source_files"));
    build.set_include_dirs(lines("include_dirs"));
    build.set_library_dirs(lines("library_dirs"));
    build.set_libraries(lines("libraries"));
    build.set_packages(lines("packages"));
    build.set_package_dirs(lines("package_dirs"));
    build.set_warnings(read_multi(ui, "warnings"));
    build.set_sanitizers(read_multi(ui, "sanitizers"));
    build.set_specific_gcc(read_multi(ui, "specific_gcc"));
    build.set_specific_clang(read_multi(ui, "specific_clang"));
    if (build.validate_configuration() != 0) {
        set_status(ui, "Ошибка параметров: проверьте флаги, пакеты и совместимость санитайзеров");
        return false;
    }
    if (build.save(path_utf8(ui->selected.config())) != 0) {
        set_status(ui, "Не удалось записать config.cfg"); return false;
    }
    ui->dirty = false;
    set_saved_state(ui, false);
    refresh_project_summary(ui);
    set_status(ui, "Настройки сохранены: " + ui->selected.name);
    return true;
}

static void load_project(Gui* ui, const project_t& project) {
    ui->selected = project;
    ui->last_artifact.clear();
    ui->output_directory = project.build();
    if (ui->result_status) gtk_label_set_text(GTK_LABEL(ui->result_status), "Проект готов к сборке");
    if (ui->result_detail) gtk_label_set_text(GTK_LABEL(ui->result_detail), "Выберите режим и нажмите «Собрать».");
    if (ui->open_folder_button) gtk_widget_set_sensitive(ui->open_folder_button, false);
    if (ui->run_artifact_button) gtk_widget_set_sensitive(ui->run_artifact_button, false);
    if (ui->build_choice) {
        const auto mode = pgr_gui_ux::load_mode(project.store_root,project.source_root);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->build_choice), static_cast<guint>(mode));
    }
    if (ui->active_project_label)
        gtk_label_set_text(GTK_LABEL(ui->active_project_label), project.name.c_str());
    ui->loading = true;
    gtk_label_set_text(GTK_LABEL(ui->project_path), path_utf8(project.source_root).c_str());
    make_t build;
    if (build.load(path_utf8(project.config())) != 0) {
        set_status(ui, "Не удалось загрузить конфигурацию: " + project.name);
        ui->loading = false;
        set_busy(ui, false);
        return;
    }
    const auto& c = build.config();
    const auto put = [ui](const char* key, const std::string& value) { set_entry(ui->fields.at(key), value); };
    const auto putlist = [ui](const char* key, const std::vector<std::string>& values) {
        set_text(ui->lists.at(key), join_lines(values));
    };
    const auto choose = [ui](const char* key, const std::string& value) {
        choice_set(ui->singles.at(key), value);
    };
    put("compiler", c.compiler);
    put("output_file", c.output_file);
    put("toolchain_file", c.toolchain_file);
    put("linker_script", c.linker_script);
    put("toolchain_prefix", c.toolchain_prefix);
    put("objcopy", c.objcopy);
    choose("compiler_id",c.compiler_id);
    choose("cmake_id",c.cmake_id);
    choose("ninja_id",c.ninja_id);
    choose("objcopy_id",c.objcopy_id);
    choose("standard", c.standard);
    choose("optimization", c.optimization);
    choose("debug", c.debug);
    choose("lto", c.lto);
    choose("arch", c.arch);
    choose("c_standard", c.c_standard);
    choose("target_type", c.target_type);
    choose("profile", c.profile);
    choose("package_manager", c.package_manager.empty() ? "none" : c.package_manager);
    putlist("source_files", c.source_files);
    putlist("include_dirs", c.include_dirs);
    putlist("library_dirs", c.library_dirs);
    putlist("libraries", c.libraries);
    putlist("packages", c.packages);
    putlist("package_dirs", c.package_dirs);
    load_multi(ui, "warnings", c.warnings);
    load_multi(ui, "sanitizers", c.sanitizers);
    load_multi(ui, "specific_gcc", c.specific_gcc);
    load_multi(ui, "specific_clang", c.specific_clang);
    ui->loading = false;
    ui->dirty = false;
    set_saved_state(ui, false);
    show_diagnostics(ui);
    show_history(ui);
    refresh_project_summary(ui);
    check_configuration(ui,false);
    set_status(ui, "Открыт проект: " + project.name);
    set_busy(ui, false);
}

struct SwitchDialog { Gui* ui; project_t next; GtkWidget* dialog; };
static void switch_destroy(gpointer value) { delete static_cast<SwitchDialog*>(value); }
static void switch_clicked(GtkButton* button, gpointer payload) {
    auto* context=static_cast<SwitchDialog*>(payload);
    const char* operation=static_cast<const char*>(g_object_get_data(G_OBJECT(button),"switch-kind"));
    const std::string type=operation?operation:"cancel";
    if (type=="save" && !save_settings(context->ui)) return;
    if (type!="cancel") load_project(context->ui,context->next);
    gtk_window_destroy(GTK_WINDOW(context->dialog));
}
static void request_switch(Gui* ui, const project_t& incoming) {
    if (!ui->dirty || ui->selected.name.empty() || ui->selected.name==incoming.name) {
        load_project(ui,incoming); return;
    }
    GtkWidget* dialog=gtk_window_new();
    auto* state=new SwitchDialog{ui,incoming,dialog};
    g_object_set_data_full(G_OBJECT(dialog),"pgr-switch",state,switch_destroy);
    gtk_window_set_title(GTK_WINDOW(dialog),"Несохранённые изменения");
    gtk_window_set_transient_for(GTK_WINDOW(dialog),GTK_WINDOW(ui->window));
    gtk_window_set_modal(GTK_WINDOW(dialog),TRUE);
    GtkWidget* body=column(12); margins(body,18);
    gtk_window_set_child(GTK_WINDOW(dialog),body);
    gtk_box_append(GTK_BOX(body),label("Сохранить изменения перед переключением проекта?"));
    GtkWidget* buttons=row(8);
    for (auto& choice : std::vector<std::pair<const char*,const char*>>{
        {"Сохранить","save"},{"Без сохранения","discard"},{"Отмена","cancel"}}) {
        auto* b=gtk_button_new_with_label(choice.first);
        g_object_set_data(G_OBJECT(b),"switch-kind",const_cast<char*>(choice.second));
        g_signal_connect(b,"clicked",G_CALLBACK(switch_clicked),state);
        gtk_box_append(GTK_BOX(buttons),b);
    }
    gtk_box_append(GTK_BOX(body),buttons);
    gtk_window_present(GTK_WINDOW(dialog));
}
static int project_row_index(Gui* ui, GtkListBoxRow* row) {
    if (!row) return -1;
    const char* name=static_cast<const char*>(g_object_get_data(G_OBJECT(row),"pgr-project-name"));
    if (!name) return -1;
    for (size_t i=0;i<ui->projects.size();++i)
        if (ui->projects[i].name==name) return static_cast<int>(i);
    return -1;
}
static void project_activated(GtkListBox*, GtkListBoxRow* row, gpointer data) {
    auto* ui=static_cast<Gui*>(data);
    if (ui->refreshing || ui->busy || !row) return;
    const int index=project_row_index(ui,row);
    if (index<0) return;
    if (!ui->project_available[static_cast<size_t>(index)]) {
        set_status(ui,"Исходники недоступны. Выберите «Изменить путь» для восстановления проекта.");
        return;
    }
    request_switch(ui,ui->projects[static_cast<size_t>(index)]);
}
static void refresh_projects(Gui* ui, const std::string& preferred) {
    const auto desired = preferred.empty() ? ui->selected.name : preferred;
    ui->refreshing = true;
    for (GtkWidget* w = gtk_widget_get_first_child(ui->project_list); w;) {
        GtkWidget* next = gtk_widget_get_next_sibling(w);
        gtk_list_box_remove(GTK_LIST_BOX(ui->project_list), w);
        w = next;
    }
    ui->projects.clear();
    ui->project_available.clear();
    for (const auto& entry : ui->registry.entries()) {
        ui->projects.push_back({entry.name,entry.source,ui->registry.projects_root()/pgr_workspace::from_utf8(entry.name)});
        ui->project_available.push_back(entry.available);
    }
    int selection = -1;
    for (size_t i = 0; i < ui->projects.size(); ++i) {
        auto* project = gtk_list_box_row_new();
        g_object_set_data_full(G_OBJECT(project),"pgr-project-name",
                               g_strdup(ui->projects[i].name.c_str()),g_free);
        GtkWidget* name = label((ui->projects[i].name +
            (ui->project_available[i] ? "" : "  [нет исходников]")).c_str());
        if (!ui->project_available[i]) gtk_widget_add_css_class(name, "pgr-unavailable");
        gtk_widget_set_margin_top(name, 9);
        gtk_widget_set_margin_bottom(name, 9);
        gtk_widget_set_margin_start(name, 10);
        gtk_widget_set_margin_end(name, 10);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(project), name);
        gtk_list_box_append(GTK_LIST_BOX(ui->project_list), project);
        if (ui->projects[i].name == desired && ui->project_available[i]) selection = static_cast<int>(i);
    }
    if (selection < 0)
        for (size_t i=0;i<ui->projects.size();++i)
            if (ui->project_available[i]) { selection=static_cast<int>(i); break; }
    ui->refreshing = false;
    if (ui->project_list) gtk_list_box_invalidate_filter(GTK_LIST_BOX(ui->project_list));
    if (selection >= 0) {
        GtkListBoxRow* selected = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ui->project_list), selection);
        gtk_list_box_select_row(GTK_LIST_BOX(ui->project_list), selected);
        load_project(ui, ui->projects[static_cast<size_t>(selection)]);
    } else {
        ui->selected = {};
        if (ui->active_project_label) gtk_label_set_text(GTK_LABEL(ui->active_project_label), "Нет проекта");
        gtk_label_set_text(GTK_LABEL(ui->project_path), "Проект не выбран");
        if (ui->summary_mode) gtk_label_set_text(GTK_LABEL(ui->summary_mode), "Нет проекта");
        if (ui->summary_compiler) gtk_label_set_text(GTK_LABEL(ui->summary_compiler), "Компилятор не выбран");
        if (ui->summary_profile) gtk_label_set_text(GTK_LABEL(ui->summary_profile), "—");
        set_status(ui, "Создайте первый проект справа");
        set_busy(ui, false);
    }
}
static void on_diagnostic_activated(GtkListBox*, GtkListBoxRow* row, gpointer data) {
    auto* ui = static_cast<Gui*>(data);
    const char* raw = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "path"));
    if (!raw || !*raw || ui->selected.name.empty()) return;
    auto path = pgr_workspace::from_utf8(raw);
    if (path.is_relative()) path = ui->selected.source_root / path;
    if (!fs::is_regular_file(path)) { set_status(ui, "Не найден файл: " + path_utf8(path)); return; }
    const int line = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "line"));
    const int col = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "col"));
    const std::string location = path_utf8(path) + ":" + std::to_string(std::max(1, line)) +
                                 ":" + std::to_string(std::max(1, col));
    const gchar* argv[] = {"code", "--goto", location.c_str(), nullptr};
    GError* error = nullptr;
    GSubprocess* editor = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_NONE, &error);
    if (editor) { g_object_unref(editor); return; }
    g_clear_error(&error);
    GFile* file = g_file_new_for_path(path_utf8(path).c_str());
    gchar* uri = g_file_get_uri(file);
    if (!g_app_info_launch_default_for_uri(uri, nullptr, &error)) {
        set_status(ui, std::string("Ошибка открытия файла: ") + (error ? error->message : "unknown"));
        g_clear_error(&error);
    }
    g_free(uri);
    g_object_unref(file);
}
static void show_diagnostics(Gui* ui) {
    for (GtkWidget* w = gtk_widget_get_first_child(ui->diagnostics_box); w;) {
        GtkWidget* next = gtk_widget_get_next_sibling(w);
        gtk_list_box_remove(GTK_LIST_BOX(ui->diagnostics_box), w);
        w = next;
    }
    if (ui->selected.name.empty()) return;
    pgr_diagnostics::collector report;
    if (!report.load(ui->selected.store_root / "diagnostics.json")) {
        gtk_label_set_text(GTK_LABEL(ui->diags_count), "Нет отчёта"); return;
    }
    int errors = 0, warnings = 0;
    std::string current_file;
    bool parent_visible=true;
    GtkListBoxRow* first_error=nullptr;
    for (const auto& d : report.records()) {
        errors += d.severity == "error";
        warnings += d.severity == "warning";
        const bool include_error = d.severity == "error";
        const bool include_warning = d.severity == "warning";
        if (include_error || include_warning) {
            parent_visible=!(include_error && ui->severity_errors &&
                 !gtk_check_button_get_active(GTK_CHECK_BUTTON(ui->severity_errors))) &&
                !(include_warning && ui->severity_warnings &&
                 !gtk_check_button_get_active(GTK_CHECK_BUTTON(ui->severity_warnings)));
        }
        if(!parent_visible) continue;
        if (d.severity!="note" && !d.file.empty() && d.file!=current_file) {
            current_file=d.file;
            auto* group=gtk_list_box_row_new();
            GtkWidget* heading=label(current_file.c_str());
            gtk_widget_add_css_class(heading,"pgr-diagnostic-group");
            margins(heading,5);
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(group),heading);
            gtk_list_box_append(GTK_LIST_BOX(ui->diagnostics_box),group);
        }
        std::string message = (d.severity=="note"?"↳ ":"") +
            std::string("[") + d.severity + "/" + d.category + "] " + d.file;
        if (d.line > 0) message += ":" + std::to_string(d.line);
        if (d.column > 0) message += ":" + std::to_string(d.column);
        message += "  " + d.message;
        GtkWidget* row_widget = gtk_list_box_row_new();
        if (d.severity == "error") {
            gtk_widget_add_css_class(row_widget, "pgr-diagnostic-error");
            if(!first_error)first_error=GTK_LIST_BOX_ROW(row_widget);
        } else if (d.severity == "warning") gtk_widget_add_css_class(row_widget, "pgr-diagnostic-warning");
        else if (d.severity == "note") gtk_widget_add_css_class(row_widget, "pgr-diagnostic-note");
        GtkWidget* line = label(message.c_str());
        gtk_label_set_wrap(GTK_LABEL(line), TRUE);
        margins(line, 6);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row_widget), line);
        g_object_set_data_full(G_OBJECT(row_widget), "path", g_strdup(d.file.c_str()), g_free);
        g_object_set_data(G_OBJECT(row_widget), "line", GINT_TO_POINTER(d.line));
        g_object_set_data(G_OBJECT(row_widget), "col", GINT_TO_POINTER(d.column));
        gtk_list_box_append(GTK_LIST_BOX(ui->diagnostics_box), row_widget);
    }
    if(first_error) gtk_list_box_select_row(GTK_LIST_BOX(ui->diagnostics_box),first_error);
    gtk_label_set_text(GTK_LABEL(ui->diags_count),
        (std::to_string(errors) + " ошибок, " + std::to_string(warnings) + " предупреждений").c_str());
}
static void diagnostic_filter_changed(GtkCheckButton*,gpointer data) {
    show_diagnostics(static_cast<Gui*>(data));
}
static void copy_diagnostic(GtkButton*,gpointer payload) {
    auto* ui=static_cast<Gui*>(payload);
    auto* row=gtk_list_box_get_selected_row(GTK_LIST_BOX(ui->diagnostics_box));
    if (!row) {set_status(ui,"Выберите сообщение в списке");return;}
    auto* text=gtk_list_box_row_get_child(row);
    if (!GTK_IS_LABEL(text)) return;
    GdkClipboard* clipboard=gtk_widget_get_clipboard(ui->window);
    gdk_clipboard_set_text(clipboard,gtk_label_get_text(GTK_LABEL(text)));
    set_status(ui,"Сообщение скопировано");
}
static void show_history(Gui* ui) {
    if (!ui->history_box) return;
    for (GtkWidget* child=gtk_widget_get_first_child(ui->history_box); child;) {
        auto* next=gtk_widget_get_next_sibling(child);
        gtk_list_box_remove(GTK_LIST_BOX(ui->history_box),child);
        child=next;
    }
    if (ui->selected.name.empty()) return;
    const auto entries=pgr_gui_ux::load_history(ui->selected.store_root/"history.log");
    if (entries.empty()) {
        GtkWidget* entry=gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(entry),label("История сборок пока пуста"));
        gtk_list_box_append(GTK_LIST_BOX(ui->history_box),entry);
        return;
    }
    for (const auto& h:entries) {
        std::time_t stamp=0;
        try {stamp=static_cast<std::time_t>(std::stoll(h.timestamp)/1000);}catch(...) {}
        char date[40]{};
        if (const auto* local=std::localtime(&stamp)) std::strftime(date,sizeof(date),"%d.%m.%Y %H:%M",local);
        const std::string result=h.result=="0"?"Успешно":h.result=="130"?"Остановлено":"Ошибка "+h.result;
        auto* line=row(18);
        margins(line,7);
        for (const auto& value : {std::string(date),result,h.mode,h.elapsed+" мс"}) {
            auto* cell=label(value.c_str());
            gtk_widget_set_hexpand(cell,TRUE);
            gtk_box_append(GTK_BOX(line),cell);
        }
        auto* entry=gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(entry),line);
        gtk_list_box_append(GTK_LIST_BOX(ui->history_box),entry);
    }
}

struct WorkMessage {
    Gui* ui;
    std::string text;
    int rc = 0;
    bool completed = false;
};
static gboolean work_on_main(gpointer payload) {
    auto* message=static_cast<WorkMessage*>(payload);
    auto* ui=message->ui;
    if (message->completed) {
        if (ui->worker.joinable()) ui->worker.join();
        const bool cancelled = message->rc == 130;
        log_line(ui,cancelled?"[PGR] Сборка остановлена":
            message->rc==0?"[PGR] Выполнено":"[PGR] Ошибка (код "+std::to_string(message->rc)+")");
        set_status(ui,cancelled?"Остановлено":message->rc==0?"Готово":"Сбой, см. журнал и диагностику");
        set_busy(ui,false);
        show_diagnostics(ui);
        show_history(ui);
        if (ui->result_status) gtk_label_set_text(GTK_LABEL(ui->result_status),
            cancelled?"Операция остановлена":message->rc==0?
            (ui->operation_is_build?"Сборка успешно завершена":"Операция выполнена"):
            (ui->operation_is_build?"Ошибка сборки":"Ошибка операции"));
        if (ui->result_detail) {
            std::string info = cancelled?"Операция прервана":message->rc==0?
                "Готово. См. журнал и артефакты ниже":"Код завершения: "+std::to_string(message->rc);
            gtk_label_set_text(GTK_LABEL(ui->result_detail),info.c_str());
        }
        // Only offer executables explicitly configured by this project and
        // actually changed by this successful direct/generated build.
        ui->last_artifact.clear();
        if(message->rc==0 && ui->operation_is_build && ui->running_mode!=pgr_gui_ux::build_mode::native) {
            make_t config;
            if(config.load(path_utf8(ui->selected.config()))==0 && !config.config().output_file.empty()) {
                const auto candidate=pgr_gui_ux::resolved(ui->selected.source_root,config.config().output_file);
                std::error_code ec;
                if(fs::is_regular_file(candidate,ec) && pgr_gui_ux::is_native_executable(candidate)) {
                    const auto modified=fs::last_write_time(candidate,ec);
                    // C++17 file_clock and system_clock epochs differ, so instead
                    // compare with the timestamp observed at operation start.
                    if(!ec && (ui->artifact_timestamp_before==fs::file_time_type::min() || modified > ui->artifact_timestamp_before)) ui->last_artifact=candidate;
                }
            }
        }
        if(ui->open_folder_button) gtk_widget_set_sensitive(ui->open_folder_button,
            fs::is_directory(ui->output_directory));
        if(ui->run_artifact_button) gtk_widget_set_sensitive(ui->run_artifact_button,!ui->last_artifact.empty());
        if(ui->results && message->rc!=0) gtk_notebook_set_current_page(GTK_NOTEBOOK(ui->results),2);
        else if(ui->results) gtk_notebook_set_current_page(GTK_NOTEBOOK(ui->results),0);
    } else log_line(ui,message->text);
    return G_SOURCE_REMOVE;
}
static void work_message_free(gpointer payload) {delete static_cast<WorkMessage*>(payload);}
static void post_work(Gui* ui, std::string message, int rc=0, bool completed=false) {
    g_idle_add_full(G_PRIORITY_DEFAULT,work_on_main,new WorkMessage{ui,std::move(message),rc,completed},work_message_free);
}
static void run_pack_cli(Gui* ui, std::vector<std::string> args,const std::string& caption) {
    if(ui->busy) {set_status(ui,"Сначала завершите текущую операцию");return;}
    if(ui->worker.joinable())ui->worker.join();
    ui->cancellation.requested.store(false);
    const auto home=ui->registry.root();
    ui->operation_is_build=false;
    gtk_text_buffer_set_text(ui->log_buffer,"",-1);
    log_line(ui,"[Pack] "+caption);
    set_busy(ui,true);
    ui->worker=std::thread([ui,home,args=std::move(args)]() {
        fs::path executable=home/
#ifdef _WIN32
           "PGR_Make.exe";
#else
           "PGR_Make";
#endif
        std::vector<std::string> command={path_utf8(executable),"--home",path_utf8(home)};
        command.insert(command.end(),args.begin(),args.end());
        pgr_process::options opt;opt.quiet=true;opt.cancel=&ui->cancellation;
        opt.output=[ui](const std::string& line){post_work(ui,line);};
        const int rc=pgr_process::run_ex(command,opt);
        post_work(ui,rc==0?"Pack: операция завершена. Для обновления списков версий перезапустите GUI.":"Pack: операция не удалась. См. журнал.",rc,true);
    });
}
static void run_cli(Gui* ui,std::vector<std::string> args,const std::string& caption) {
    if (ui->busy || ui->selected.name.empty()) return;
    if (ui->worker.joinable()) ui->worker.join();
    ui->cancellation.requested.store(false);
    const bool main_build=std::find(args.begin(),args.end(),"--ci")!=args.end();
    ui->operation_is_build=main_build;
    const bool direct=std::find(args.begin(),args.end(),"--direct")!=args.end();
    const bool native=std::find(args.begin(),args.end(),"--native-cmake")!=args.end();
    const project_t project=ui->selected;
    const auto home=ui->registry.root(),pack=ui->registry.pack_root();
    const auto compiler=get_entry(ui->fields.at("compiler"));
    ui->artifact_timestamp_before = fs::file_time_type::min();
    make_t current;
    if(current.load(path_utf8(project.config()))==0 && !current.config().output_file.empty()) {
        auto candidate=pgr_gui_ux::resolved(project.source_root,current.config().output_file);
        std::error_code ec;
        const auto previous=fs::last_write_time(candidate,ec);
        if(!ec) ui->artifact_timestamp_before=previous;
    }
    make_t build_config;
    if (build_config.load(path_utf8(project.config()))==0)
        ui->output_directory=pgr_build::default_build_directory(project,build_config.config());
    else ui->output_directory=project.build();
    gtk_text_buffer_set_text(ui->log_buffer,"",-1);
    log_line(ui,"[PGR] "+caption+" · "+project.name);
    set_status(ui,"Выполняется: "+caption);
    set_busy(ui,true);
    ui->worker=std::thread([ui,main_build,direct,native,project,home,pack,args=std::move(args),compiler]() {
        int rc=-1;
        if (main_build) {
            pgr_build::options opt; opt.pack=pack; opt.native_cmake=native; opt.direct=direct;
            pgr_build::hooks hooks;
            hooks.cancel=&ui->cancellation;
            hooks.on_output=[ui](const std::string& text){post_work(ui,text);};
            rc=pgr_build::run(project,opt,hooks);
        } else {
            fs::path executable=home/
#ifdef _WIN32
                "PGR_Make.exe";
#else
                "PGR_Make";
#endif
            std::vector<std::string> command={path_utf8(executable),"--home",path_utf8(home),"--project",project.name};
            command.insert(command.end(),args.begin(),args.end());
            pgr_process::options opt;
            opt.quiet=true;
            opt.cancel=&ui->cancellation;
            opt.working_directory=project.source_root;
            opt.output=[ui](const std::string& line){post_work(ui,line);};
            rc=pgr_process::run_ex(command,opt);
        }
        post_work(ui,"",rc,true);
    });
}

struct CreateDialog {
    Gui* ui;
    GtkWidget *window, *name, *source, *starter;
};
static void create_dialog_destroy(gpointer ptr) { delete static_cast<CreateDialog*>(ptr); }
static void create_folder_finished(GObject* object, GAsyncResult* result, gpointer payload) {
    GtkWidget* entry = GTK_WIDGET(payload);
    GError* error = nullptr;
    GFile* file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(object), result, &error);
    g_clear_error(&error);
    if (file) {
        gchar* name = g_file_get_path(file);
        if (name) { set_entry(entry, name); g_free(name); }
        g_object_unref(file);
    }
    g_object_unref(entry);
}
static void create_browse(GtkButton*, gpointer ptr) {
    auto* dialog = static_cast<CreateDialog*>(ptr);
    GtkFileDialog* chooser = gtk_file_dialog_new();
    gtk_file_dialog_set_title(chooser, "Исходники проекта");
    gtk_file_dialog_select_folder(chooser, GTK_WINDOW(dialog->window), nullptr,
                                  create_folder_finished, g_object_ref(dialog->source));
    g_object_unref(chooser);
}
static void create_project(GtkButton*, gpointer ptr) {
    auto* dialog = static_cast<CreateDialog*>(ptr);
    const std::string name = get_entry(dialog->name);
    const auto directory = pgr_workspace::from_utf8(get_entry(dialog->source));
    project_t created;
    std::string error;
    if (!pgr_project::create_project(dialog->ui->registry, name, directory,
            gtk_check_button_get_active(GTK_CHECK_BUTTON(dialog->starter)), created, error)) {
        set_status(dialog->ui, "Ошибка создания: " + error);
        return;
    }
    refresh_projects(dialog->ui, name);
    gtk_window_destroy(GTK_WINDOW(dialog->window));
}
static void open_create_dialog(Gui* ui) {
    auto* dlg = new CreateDialog{};
    dlg->ui = ui;
    dlg->window = gtk_window_new();
    g_object_set_data_full(G_OBJECT(dlg->window), "pgr-create-context", dlg, create_dialog_destroy);
    gtk_window_set_transient_for(GTK_WINDOW(dlg->window), GTK_WINDOW(ui->window));
    gtk_window_set_modal(GTK_WINDOW(dlg->window), TRUE);
    gtk_window_set_title(GTK_WINDOW(dlg->window), "Новый проект");
    gtk_window_set_default_size(GTK_WINDOW(dlg->window), 520, 250);
    GtkWidget* content = column(10);
    margins(content);
    gtk_window_set_child(GTK_WINDOW(dlg->window), content);
    gtk_box_append(GTK_BOX(content), label("Название проекта"));
    dlg->name = gtk_entry_new();
    gtk_box_append(GTK_BOX(content), dlg->name);
    gtk_box_append(GTK_BOX(content), label("Каталог исходников"));
    GtkWidget* source_row = row();
    dlg->source = gtk_entry_new();
    gtk_widget_set_hexpand(dlg->source, TRUE);
    gtk_box_append(GTK_BOX(source_row), dlg->source);
    GtkWidget* browse_button = gtk_button_new_with_label("Обзор…");
    gtk_box_append(GTK_BOX(source_row), browse_button);
    g_signal_connect(browse_button, "clicked", G_CALLBACK(create_browse), dlg);
    gtk_box_append(GTK_BOX(content), source_row);
    dlg->starter = gtk_check_button_new_with_label("Создать пример main.cpp при необходимости");
    gtk_box_append(GTK_BOX(content), dlg->starter);
    GtkWidget* confirm = gtk_button_new_with_label("Создать проект");
    gtk_widget_add_css_class(confirm, "suggested-action");
    g_signal_connect(confirm, "clicked", G_CALLBACK(create_project), dlg);
    gtk_box_append(GTK_BOX(content), confirm);
    gtk_window_present(GTK_WINDOW(dlg->window));
}
struct ProjectOperation {
    Gui* ui;
    std::string name, kind;
    GtkWidget *dialog, *entry;
};
static void project_operation_destroy(gpointer ptr) {delete static_cast<ProjectOperation*>(ptr);}
static void project_operation_confirm(GtkButton*, gpointer payload) {
    auto* state=static_cast<ProjectOperation*>(payload);
    std::string error;
    auto* ui=state->ui;
    const std::string value=get_entry(state->entry);
    bool success=false;
    if (state->kind=="rename") success=ui->registry.rename_project(state->name,value,error);
    else if (state->kind=="relocate") success=ui->registry.relocate(state->name,pgr_workspace::from_utf8(value),error);
    else if (state->kind=="archive") success=ui->registry.archive(state->name,error);
    else if (state->kind=="clone") success=ui->registry.clone_project(state->name,value,error);
    if (!success) {set_status(ui,"Операция не выполнена: "+error);return;}
    if (ui->selected.name==state->name) {ui->selected={};ui->dirty=false;}
    const std::string next=state->kind=="rename" || state->kind=="clone"?value:"";
    gtk_window_destroy(GTK_WINDOW(state->dialog));
    refresh_projects(ui,next);
    set_status(ui,"Запись проекта обновлена. Исходные файлы не затронуты.");
}
static void open_project_operation(Gui* ui,const std::string& kind) {
    if (ui->busy || ui->dirty) {
        set_status(ui,"Перед управлением реестром сохраните настройки и завершите сборку");return;
    }
    GtkListBoxRow* selected=gtk_list_box_get_selected_row(GTK_LIST_BOX(ui->project_list));
    if (!selected) {set_status(ui,"Выберите проект справа");return;}
    const int index=project_row_index(ui,selected);
    if (index<0) return;
    const auto& item=ui->projects[static_cast<size_t>(index)];
    auto* dialog=gtk_window_new();
    const std::string title=kind=="rename"?"Переименовать проект":
        kind=="relocate"?"Изменить каталог исходников":
        kind=="clone"?"Клонировать настройки проекта":"Убрать проект из списка";
    gtk_window_set_title(GTK_WINDOW(dialog),title.c_str());
    gtk_window_set_transient_for(GTK_WINDOW(dialog),GTK_WINDOW(ui->window));
    gtk_window_set_modal(GTK_WINDOW(dialog),TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog),540,-1);
    auto* state=new ProjectOperation{ui,item.name,kind,dialog,nullptr};
    g_object_set_data_full(G_OBJECT(dialog),"project-operation",state,project_operation_destroy);
    auto* box=column(10);margins(box,16);
    gtk_window_set_child(GTK_WINDOW(dialog),box);
    gtk_box_append(GTK_BOX(box),label(("Проект: "+item.name).c_str()));
    if (kind=="archive")
        gtk_box_append(GTK_BOX(box),label("Будут архивированы только файлы реестра. Исходники и build сохраняются."));
    else {
        gtk_box_append(GTK_BOX(box),label(kind=="rename" || kind=="clone"?"Новое имя:":"Существующий каталог исходников:"));
        state->entry=gtk_entry_new();
        gtk_editable_set_text(GTK_EDITABLE(state->entry),
            kind=="rename"?item.name.c_str():kind=="clone"?"":path_utf8(item.source_root).c_str());
        gtk_box_append(GTK_BOX(box),state->entry);
    }
    auto* button=gtk_button_new_with_label(kind=="archive"?"Архивировать запись":"Применить");
    if (kind=="archive") gtk_widget_add_css_class(button,"destructive-action");
    g_signal_connect(button,"clicked",G_CALLBACK(project_operation_confirm),state);
    gtk_box_append(GTK_BOX(box),button);
    gtk_window_present(GTK_WINDOW(dialog));
}
static gboolean filter_project(GtkListBoxRow* row,gpointer data) {
    auto* ui=static_cast<Gui*>(data);
    const char* query=gtk_editable_get_text(GTK_EDITABLE(ui->project_search));
    if (!query || !*query) return TRUE;
    const int index=project_row_index(ui,row);
    if (index<0) return TRUE;
    const auto& record=ui->projects[static_cast<size_t>(index)];
    gchar* needle=g_utf8_casefold(query,-1);
    gchar* name=g_utf8_casefold(record.name.c_str(),-1);
    gchar* source=g_utf8_casefold(path_utf8(record.source_root).c_str(),-1);
    gboolean matches=(strstr(name,needle)!=nullptr || strstr(source,needle)!=nullptr);
    g_free(needle);g_free(name);g_free(source);
    return matches;
}
static void search_projects_changed(GtkEditable*,gpointer data) {
    auto* ui=static_cast<Gui*>(data);
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(ui->project_list));
}
// Search across labels inside a section, not only its heading. This keeps
// seldom-used flags discoverable without requiring users to know their group.
static bool contains_label(GtkWidget* widget, const char* query, int depth = 0) {
    if (!widget || depth > 18) return false;
    if (GTK_IS_LABEL(widget)) {
        gchar* text = g_utf8_casefold(gtk_label_get_text(GTK_LABEL(widget)), -1);
        const bool matches = text && strstr(text, query) != nullptr;
        g_free(text);
        if (matches) return true;
    }
    for (GtkWidget* child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child)) {
        if (contains_label(child, query, depth + 1)) return true;
    }
    return false;
}
static void update_settings_filter(Gui* ui) {
    const char* current = ui->settings_search ?
        gtk_editable_get_text(GTK_EDITABLE(ui->settings_search)) : "";
    gchar* query = g_utf8_casefold(current, -1);
    const guint category = ui->category_select ?
        gtk_drop_down_get_selected(GTK_DROP_DOWN(ui->category_select)) : 0;
    int section_index = 0;
    for (GtkWidget* child = gtk_widget_get_first_child(ui->settings_form); child;
         child = gtk_widget_get_next_sibling(child), ++section_index) {
        const char* caption = GTK_IS_EXPANDER(child) ? gtk_expander_get_label(GTK_EXPANDER(child)) : "";
        gchar* lowered = g_utf8_casefold(caption ? caption : "", -1);
        // Actual source section order: general, versions, flags, paths, cmake, actions.
        const guint section_category[] = {1, 5, 2, 3, 4, 6};
        const bool group_ok = category == 0 ||
            (section_index < 6 && category == section_category[section_index]);
        const bool matches = !*query || strstr(lowered, query) != nullptr ||
                             contains_label(child, query);
        gtk_widget_set_visible(child, group_ok && matches);
        if (((*query && matches) || category != 0) && group_ok && GTK_IS_EXPANDER(child))
            gtk_expander_set_expanded(GTK_EXPANDER(child), TRUE);
        g_free(lowered);
    }
    g_free(query);
}
static void settings_search_changed(GtkEditable*, gpointer payload) {
    update_settings_filter(static_cast<Gui*>(payload));
}
static void category_changed(GObject*, GParamSpec*, gpointer payload) {
    auto* ui = static_cast<Gui*>(payload);
    update_settings_filter(ui);
    schedule_layout_save(ui);
}
static void refresh_project_summary(Gui* ui) {
    if (!ui->summary_compiler) return;
    if (ui->selected.name.empty()) return;
    std::string compiler=get_entry(ui->fields.at("compiler"));
    const std::string pin=choice_value(ui->singles.at("compiler_id"));
    if(!pin.empty()) compiler=pin;
    if(compiler.empty()) compiler="Автоматически";
    gtk_label_set_text(GTK_LABEL(ui->summary_compiler),compiler.c_str());
    gtk_widget_set_tooltip_text(ui->summary_compiler,compiler.c_str());
    const auto profile=choice_value(ui->singles.at("profile"));
    gtk_label_set_text(GTK_LABEL(ui->summary_profile),profile.empty()?"Текущие настройки":profile.c_str());
    static const char* modes[]={"PGR CMake","Существующий CMake","Прямая сборка"};
    const guint index=ui->build_choice?gtk_drop_down_get_selected(GTK_DROP_DOWN(ui->build_choice)):0;
    gtk_label_set_text(GTK_LABEL(ui->summary_mode),modes[std::min(index,2u)]);
    const auto cmake_id=choice_value(ui->singles.at("cmake_id"));
    gtk_label_set_text(GTK_LABEL(ui->summary_cmake),
        cmake_id.empty()?"CMake: PATH (не закреплён)":cmake_id.c_str());
    gtk_widget_set_tooltip_text(ui->summary_cmake,
        cmake_id.empty()?"Используется системный PATH; версия не закреплена":cmake_id.c_str());
}
static bool check_configuration(Gui* ui, bool focus_error) {
    if(ui->selected.name.empty()) return false;
    const auto mode=static_cast<pgr_gui_ux::build_mode>(
        std::min(gtk_drop_down_get_selected(GTK_DROP_DOWN(ui->build_choice)),2u));
    const auto issues=pgr_gui_ux::preflight(ui->selected.source_root,mode,
        get_entry(ui->fields.at("compiler")),
        split_lines(text_contents(ui->lists.at("source_files"))),
        split_lines(text_contents(ui->lists.at("include_dirs"))),
        get_entry(ui->fields.at("toolchain_file")));
    // Show missing pinned tools early, rather than silently falling back to PATH.
    std::vector<pgr_gui_ux::issue> all=issues;
    ui->missing_pin.clear();
    pgr_pack_manager::manager manager(ui->registry.pack_root(),ui->registry.root());
    std::string error;
    if(manager.load(error)) for (const auto& tool : {"compiler_id","cmake_id","ninja_id","objcopy_id"}) {
        const auto id=choice_value(ui->singles.at(tool));
        if(!id.empty()) {
            const auto* item=manager.find(id);
            std::error_code ec;
            // On project load inspect only installed files. Full SHA-256 validation
            // runs when the user explicitly checks settings or launches a build.
            const bool installed=item && item->platform==pgr_pack_manager::platform() &&
                fs::is_regular_file(pgr_pack_manager::executable_path(*item),ec);
            const bool valid=installed && (!focus_error || pgr_pack_manager::verify_tool(*item,error));
            if(!valid) {
                if(ui->missing_pin.empty()) ui->missing_pin=id;
                all.push_back({tool,"Версия недоступна или повреждена: "+id});
            }
        }
    }
    for(const auto& field : {"compiler","toolchain_file"})
        gtk_widget_remove_css_class(ui->fields.at(field),"pgr-invalid");
    for(const auto& field : {"source_files","include_dirs"})
        gtk_widget_remove_css_class(ui->lists.at(field),"pgr-invalid");
    for(const auto& issue:all) {
        auto field=ui->fields.find(issue.field);
        if(field!=ui->fields.end()) gtk_widget_add_css_class(field->second,"pgr-invalid");
        auto list=ui->lists.find(issue.field);
        if(list!=ui->lists.end()) gtk_widget_add_css_class(list->second,"pgr-invalid");
    }
    if(ui->validation_label) gtk_label_set_text(GTK_LABEL(ui->validation_label),
        all.empty()?"✓ Основные настройки проверены":all.front().message.c_str());
    if(ui->install_missing_button) gtk_widget_set_visible(ui->install_missing_button,
        !ui->missing_pin.empty());
    if(!all.empty() && focus_error) {
        set_status(ui,all.front().message);
        // Native CMake issues are located on the main page, other paths on paths page.
        if(ui->category_select) gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->category_select),
            all.front().field=="build_mode"?1:all.front().field=="compiler"?1:3);
    }
    return all.empty();
}
static void open_pack_manager(Gui* ui);
static void install_missing_clicked(GtkButton*,gpointer payload) {
    open_pack_manager(static_cast<Gui*>(payload));
}
static void check_button_clicked(GtkButton*,gpointer payload) {
    auto* ui=static_cast<Gui*>(payload);
    if(check_configuration(ui,true)) set_status(ui,"Основные пути и закреплённые инструменты проверены");
}
static void on_mode_changed(GObject*,GParamSpec*,gpointer data) {
    auto* ui=static_cast<Gui*>(data);
    if(!ui->loading && !ui->selected.name.empty() && ui->build_choice) {
        const guint index=gtk_drop_down_get_selected(GTK_DROP_DOWN(ui->build_choice));
        if(!pgr_gui_ux::save_mode(ui->selected.store_root,
               static_cast<pgr_gui_ux::build_mode>(std::min(index,2u))))
            set_status(ui,"Не удалось сохранить режим сборки");
    }
    refresh_project_summary(ui);
}
static void primary_build(GtkButton*,gpointer data) {
    auto* ui=static_cast<Gui*>(data);
    if (ui->busy || ui->selected.name.empty()) return;
    if (!check_configuration(ui,true) || !save_settings(ui)) return;
    guint mode=gtk_drop_down_get_selected(GTK_DROP_DOWN(ui->build_choice));
    ui->running_mode=static_cast<pgr_gui_ux::build_mode>(std::min(mode,2u));
    if (mode==1) run_cli(ui,{"--ci","--native-cmake"},"Сборка существующего CMake");
    else if (mode==2) run_cli(ui,{"--ci","--direct"},"Прямая сборка");
    else run_cli(ui,{"--ci"},"Сборка PGR CMake");
}
struct CompilerChoice { Gui* ui; std::string path; };
static void release_compiler_choice(gpointer ptr, GClosure*) { delete static_cast<CompilerChoice*>(ptr); }
static void compiler_picked(GtkButton* button, gpointer ptr) {
    auto* choice = static_cast<CompilerChoice*>(ptr);
    set_entry(choice->ui->fields["compiler"], choice->path);
    GtkWidget* window = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW);
    if (window) gtk_window_destroy(GTK_WINDOW(window));
}
static void open_pack_dialog(Gui* ui) {
    const auto candidates = pgr_pack::compilers(ui->registry.pack_root());
    if (candidates.empty()) {
        set_status(ui, "В Pack нет компиляторов. Используйте поле «Компилятор»."); return;
    }
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Компиляторы из Pack");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(ui->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 590, 360);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_window_set_child(GTK_WINDOW(dialog), scroll);
    GtkWidget* list = column(4);
    margins(list);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    for (const auto& path : candidates) {
        const auto value = path_utf8(path);
        GtkWidget* button = gtk_button_new_with_label(value.c_str());
        gtk_box_append(GTK_BOX(list), button);
        auto* choice = new CompilerChoice{ui, value};
        g_signal_connect_data(button, "clicked", G_CALLBACK(compiler_picked),
                              choice, release_compiler_choice, G_CONNECT_DEFAULT);
    }
    gtk_window_present(GTK_WINDOW(dialog));
}
struct SaveProfileDialog { Gui* ui; GtkWidget *window, *name; };
static void save_profile_context(gpointer ptr) { delete static_cast<SaveProfileDialog*>(ptr); }
static void finish_save_profile(GtkButton*, gpointer payload) {
    auto* dlg = static_cast<SaveProfileDialog*>(payload);
    auto* ui = dlg->ui;
    const std::string name = get_entry(dlg->name);
    if (!pgr_workspace::valid_name(name)) { set_status(ui, "Недопустимое имя профиля"); return; }
    if (!save_settings(ui)) return;
    const fs::path location = ui->selected.profiles() / pgr_workspace::from_utf8(name + ".cfg");
    std::error_code ec;
    fs::create_directories(location.parent_path(), ec);
    if (ec || fs::exists(location)) { set_status(ui, "Профиль существует или недоступен: " + name); return; }
    make_t config;
    if (config.load(path_utf8(ui->selected.config())) != 0) return;
    config.set_profile(name);
    if (config.save(path_utf8(location)) != 0) { set_status(ui, "Не удалось сохранить профиль"); return; }
    set_status(ui, "Сохранён профиль: " + name);
    gtk_window_destroy(GTK_WINDOW(dlg->window));
}
static void open_save_profile(Gui* ui) {
    if (ui->selected.name.empty()) return;
    auto* dialog = new SaveProfileDialog{};
    dialog->ui = ui;
    dialog->window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog->window), "Сохранение профиля");
    gtk_window_set_transient_for(GTK_WINDOW(dialog->window), GTK_WINDOW(ui->window));
    gtk_window_set_modal(GTK_WINDOW(dialog->window), TRUE);
    g_object_set_data_full(G_OBJECT(dialog->window), "profile-dialog", dialog, save_profile_context);
    GtkWidget* body = column(8);
    margins(body);
    gtk_window_set_child(GTK_WINDOW(dialog->window), body);
    gtk_box_append(GTK_BOX(body), label("Имя нового профиля:"));
    dialog->name = gtk_entry_new();
    gtk_box_append(GTK_BOX(body), dialog->name);
    GtkWidget* button = gtk_button_new_with_label("Сохранить");
    g_signal_connect(button, "clicked", G_CALLBACK(finish_save_profile), dialog);
    gtk_box_append(GTK_BOX(body), button);
    gtk_window_present(GTK_WINDOW(dialog->window));
}
struct ProfileLoadState { Gui* ui; std::string project_name; };
static void profile_load_finished(GObject* object, GAsyncResult* result, gpointer payload) {
    std::unique_ptr<ProfileLoadState> state(static_cast<ProfileLoadState*>(payload));
    auto* ui = state->ui;
    GError* error = nullptr;
    GFile* file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(object), result, &error);
    g_clear_error(&error);
    if (!file) return;
    gchar* path = g_file_get_path(file);
    if (path) {
        make_t profile;
        const fs::path file_path = pgr_workspace::from_utf8(path);
        if (profile.load(path) != 0) {
            set_status(ui, "Не удалось загрузить выбранный профиль");
        } else if (ui->selected.name != state->project_name) {
            set_status(ui, "Проект изменён, загрузка профиля отменена");
        } else {
            profile.set_profile(file_path.stem().u8string());
            if (profile.save(path_utf8(ui->selected.config())) == 0) {
                load_project(ui, ui->selected);
                set_status(ui, "Загружен профиль: " + file_path.stem().u8string());
            } else set_status(ui, "Не удалось сохранить настройки профиля");
        }
        g_free(path);
    }
    g_object_unref(file);
}
static void open_load_profile(Gui* ui) {
    if (ui->selected.name.empty()) return;
    GtkFileDialog* chooser = gtk_file_dialog_new();
    gtk_file_dialog_set_title(chooser, "Выберите файл профиля (*.cfg)");
    gtk_file_dialog_open(chooser, GTK_WINDOW(ui->window), nullptr, profile_load_finished,
                         new ProfileLoadState{ui, ui->selected.name});
    g_object_unref(chooser);
}
static void on_profile_changed(GObject*, GParamSpec*, gpointer payload) {
    auto* ui = static_cast<Gui*>(payload);
    if (ui->loading) return;
    const std::string profile = choice_value(ui->singles.at("profile"));
    if (profile == "debug") {
        choice_set(ui->singles.at("optimization"), "-O0");
        choice_set(ui->singles.at("debug"), "-g");
    } else if (profile == "release") {
        choice_set(ui->singles.at("optimization"), "-O2");
        choice_set(ui->singles.at("debug"), "");
    }
}
static void confirm_clean(GtkButton*, gpointer ptr) {
    auto* ui = static_cast<Gui*>(ptr);
    GtkWidget* window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(window), "Подтверждение очистки");
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(ui->window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    GtkWidget* body = column(10);
    margins(body);
    gtk_window_set_child(GTK_WINDOW(window), body);
    gtk_box_append(GTK_BOX(body), label("Удалить только принадлежащий PGR_Make каталог сборки?"));
    GtkWidget* confirm = gtk_button_new_with_label("Очистить");
    gtk_widget_add_css_class(confirm, "destructive-action");
    gtk_box_append(GTK_BOX(body), confirm);
    // Hold the GTK dialog alive only until the click callback returns.
    g_signal_connect(confirm, "clicked", G_CALLBACK(+[](GtkButton* button, gpointer payload) {
        auto* gui = static_cast<Gui*>(payload);
        GtkWidget* dialog = gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW);
        if (dialog) gtk_window_destroy(GTK_WINDOW(dialog));
        run_cli(gui, {"--clean-only"}, "Очистка каталога сборки");
    }), ui);
    gtk_window_present(GTK_WINDOW(window));
}
struct PackAction {Gui* ui;std::string id,operation;};
static void dispose_pack_action(gpointer data,GClosure*) {delete static_cast<PackAction*>(data);}
static void pack_zip_done(GObject* object,GAsyncResult* result,gpointer data) {
    std::unique_ptr<PackAction> action(static_cast<PackAction*>(data));
    GError* error=nullptr;
    GFile* file=gtk_file_dialog_open_finish(GTK_FILE_DIALOG(object),result,&error);
    if(file) {
        gchar* filename=g_file_get_path(file);
        if(filename){run_pack_cli(action->ui,{"--pack-import",action->id,filename},"Импорт локального ZIP "+action->id);g_free(filename);}
        g_object_unref(file);
    }
    g_clear_error(&error);
}
static void pack_retire_confirmed(GtkButton* button,gpointer data) {
    auto* action=static_cast<PackAction*>(data);
    GtkWidget* window=gtk_widget_get_ancestor(GTK_WIDGET(button),GTK_TYPE_WINDOW);
    if(window)gtk_window_close(GTK_WINDOW(window));
    run_pack_cli(action->ui,{"--pack-retire",action->id},"Карантин версии "+action->id);
}
static void pack_bulk_confirmed(GtkButton* button,gpointer data) {
    auto* ui=static_cast<Gui*>(data);
    GtkWindow* confirmation=GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(button),GTK_TYPE_WINDOW));
    GtkWindow* pack_dialog=gtk_window_get_transient_for(confirmation);
    // Dismiss both modal windows before starting the asynchronous CLI process.
    gtk_window_destroy(confirmation);
    if(pack_dialog) gtk_window_destroy(pack_dialog);
    run_pack_cli(ui,{"--pack-install-all"},"Установка актуального набора GCC / Clang / CMake / Ninja");
}
static void pack_action_clicked(GtkButton* button,gpointer data) {
    auto* item=static_cast<PackAction*>(data);
    if(item->operation=="all") {
        pgr_pack_manager::manager registry(item->ui->registry.pack_root(),item->ui->registry.root());
        std::string error;
        if(!registry.load(error)) {set_status(item->ui,"Pack: "+error);return;}
        const auto available=pgr_pack_bulk::merged_catalog(item->ui->registry.root(),item->ui->registry.pack_root(),error);
        if(!error.empty()) {set_status(item->ui,"Pack: "+error);return;}
        const auto plan=pgr_pack_bulk::latest(available,registry);
        GtkWidget* parent=gtk_widget_get_ancestor(GTK_WIDGET(button),GTK_TYPE_WINDOW);
        GtkWidget* confirm=gtk_window_new();
        gtk_widget_add_css_class(confirm,"pgr-dialog");
        gtk_window_set_title(GTK_WINDOW(confirm),"Пакетная установка инструментов");
        gtk_window_set_transient_for(GTK_WINDOW(confirm),GTK_WINDOW(parent));
        gtk_window_set_modal(GTK_WINDOW(confirm),TRUE);
        gtk_window_set_default_size(GTK_WINDOW(confirm),560,260);
        GtkWidget* body=column(12);margins(body,18);
        gtk_window_set_child(GTK_WINDOW(confirm),body);
        GtkWidget* intro=label("Будут установлены последние проверенные версии из каталога Pack.\nСуществующие версии сохраняются; большие архивы могут потребовать несколько ГБ диска.");
        gtk_label_set_wrap(GTK_LABEL(intro),TRUE);
        gtk_box_append(GTK_BOX(body),intro);
        for(const auto& pkg:plan.downloads) {
            std::string description="• "+pkg.id;
            for(const auto& companion:pkg.provides) description+=" + "+companion.tool+" "+companion.version;
            gtk_box_append(GTK_BOX(body),label(description.c_str()));
        }
        for(const auto& entry:plan.unavailable) gtk_box_append(GTK_BOX(body),label(("Нет пакета: "+entry).c_str()));
        for(const auto& entry:plan.invalid) gtk_box_append(GTK_BOX(body),label(("Проверка не пройдена: "+entry).c_str()));
        if(plan.downloads.empty()&&plan.ready()) gtk_box_append(GTK_BOX(body),label("Все необходимые версии уже установлены."));
        GtkWidget* actions=row(8);
        gtk_box_append(GTK_BOX(body),actions);
        GtkWidget* cancel=gtk_button_new_with_label("Отмена");
        gtk_box_append(GTK_BOX(actions),cancel);
        g_signal_connect_swapped(cancel,"clicked",G_CALLBACK(gtk_window_close),confirm);
        GtkWidget* start=gtk_button_new_with_label("Скачать и установить");
        gtk_widget_add_css_class(start,"suggested-action");
        gtk_widget_set_sensitive(start,plan.ready()&&!plan.downloads.empty());
        gtk_box_append(GTK_BOX(actions),start);
        g_signal_connect(start,"clicked",G_CALLBACK(pack_bulk_confirmed),item->ui);
        gtk_window_present(GTK_WINDOW(confirm));
        return;
    }
    if(item->operation=="import") {
        GtkFileDialog* chooser=gtk_file_dialog_new();
        gtk_file_dialog_set_title(chooser,"Локальный архив инструмента (*.zip)");
        gtk_file_dialog_open(chooser,GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(button),GTK_TYPE_WINDOW)),nullptr,
                             pack_zip_done,new PackAction(*item));
        g_object_unref(chooser);
        return;
    }
    if(item->operation=="retire") {
        GtkWidget* prompt=gtk_window_new();
        gtk_window_set_title(GTK_WINDOW(prompt),"Подтвердите вывод версии из Pack");
        gtk_window_set_transient_for(GTK_WINDOW(prompt),GTK_WINDOW(item->ui->window));
        gtk_window_set_modal(GTK_WINDOW(prompt),TRUE);
        GtkWidget* body=column(12);margins(body,16);
        gtk_window_set_child(GTK_WINDOW(prompt),body);
        gtk_box_append(GTK_BOX(body),label(("Переместить "+item->id+" в Pack/Retired?\nИспользуемые проектами версии не удаляются.").c_str()));
        GtkWidget* confirm=gtk_button_new_with_label("Переместить в карантин");
        gtk_widget_add_css_class(confirm,"destructive-action");
        gtk_box_append(GTK_BOX(body),confirm);
        g_signal_connect_data(confirm,"clicked",G_CALLBACK(pack_retire_confirmed),new PackAction(*item),dispose_pack_action,G_CONNECT_DEFAULT);
        gtk_window_present(GTK_WINDOW(prompt));
        return;
    }
    std::vector<std::string> command;
    if(item->operation=="install")command={"--pack-install",item->id};
    GtkWidget* parent=gtk_widget_get_ancestor(GTK_WIDGET(button),GTK_TYPE_WINDOW);
    if(parent)gtk_window_close(GTK_WINDOW(parent));
    run_pack_cli(item->ui,command,item->operation+" "+item->id);
}
static void pack_button(Gui* ui,GtkWidget* row,const char* title,
                        const std::string& operation,const std::string& id) {
    GtkWidget* button=gtk_button_new_with_label(title);
    gtk_box_append(GTK_BOX(row),button);
    auto* action=new PackAction{ui,id,operation};
    g_signal_connect_data(button,"clicked",G_CALLBACK(pack_action_clicked),action,dispose_pack_action,G_CONNECT_DEFAULT);
}
static void open_pack_manager(Gui* ui) {
    if(ui->busy){set_status(ui,"Pack занят: завершите текущую сборку");return;}
    pgr_pack_manager::manager manager(ui->registry.pack_root(),ui->registry.root());
    std::string error;
    if(!manager.load(error)){set_status(ui,"Ошибка Pack: "+error);return;}
    GtkWidget* window=gtk_window_new();
    gtk_widget_add_css_class(window, "pgr-dialog");
    gtk_window_set_title(GTK_WINDOW(window),"Pack Manager · версии и установка инструментов");
    gtk_window_set_transient_for(GTK_WINDOW(window),GTK_WINDOW(ui->window));
    gtk_window_set_modal(GTK_WINDOW(window),TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window),900,620);
    GtkWidget* scroll=gtk_scrolled_window_new();
    GtkWidget* body=column(10);margins(body,16);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),body);
    gtk_window_set_child(GTK_WINDOW(window),scroll);
    // The main action performs one background CLI operation. The CLI uses
    // SHA-256 verification, staging, version checks and the Pack registry.
    GtkWidget* bulk=row(10);
    gtk_box_append(GTK_BOX(body),bulk);
    GtkWidget* bulk_text=label("Быстрая установка: GCC + Clang + CMake + Ninja (последние проверенные версии каталога)");
    gtk_widget_set_hexpand(bulk_text,TRUE);
    gtk_label_set_wrap(GTK_LABEL(bulk_text),TRUE);
    gtk_box_append(GTK_BOX(bulk),bulk_text);
    pack_button(ui,bulk,"Скачать необходимые инструменты","all","");
    gtk_box_append(GTK_BOX(body),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    if(!ui->missing_pin.empty()) {
        auto* missing=label(("Требуется проекту: "+ui->missing_pin+". Проверьте доступные версии ниже.").c_str());
        gtk_label_set_wrap(GTK_LABEL(missing),TRUE);
        gtk_widget_add_css_class(missing,"pgr-validation-label");
        gtk_box_append(GTK_BOX(body),missing);
    }
    GtkWidget* heading=label("Установленные инструменты");gtk_widget_add_css_class(heading,"title-3");gtk_box_append(GTK_BOX(body),heading);
    if(manager.entries().empty())gtk_box_append(GTK_BOX(body),label("Нет зарегистрированных версий. Можно импортировать архив или установить из каталога."));
    for(const auto& installed:manager.entries()) {
        std::string why;
        const bool healthy=pgr_pack_manager::verify_tool(installed,why);
        GtkWidget* line=row(8);
        GtkWidget* desc=label((installed.id+"  ["+(healthy?"проверен":"недоступен")+"]\n"+
                               path_utf8(pgr_pack_manager::executable_path(installed))).c_str());
        gtk_widget_set_hexpand(desc,TRUE);
        gtk_box_append(GTK_BOX(line),desc);
        pack_button(ui,line,"Убрать из Pack","retire",installed.id);
        gtk_box_append(GTK_BOX(body),line);
        gtk_box_append(GTK_BOX(body),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    }
    GtkWidget* catalog_title=label("Каталог доступных версий · официальные дистрибутивы с SHA-256");
    gtk_widget_add_css_class(catalog_title,"title-3");gtk_box_append(GTK_BOX(body),catalog_title);
    std::vector<pgr_pack_manager::package> available=pgr_pack_bulk::merged_catalog(ui->registry.root(),ui->registry.pack_root(),error);
    if(!error.empty())gtk_box_append(GTK_BOX(body),label(error.c_str()));
    if(!ui->missing_pin.empty()) std::stable_partition(available.begin(),available.end(),
        [ui](const auto& p){return p.id==ui->missing_pin;});
    for(const auto& pkg:available) {
        GtkWidget* line=row(8);
        GtkWidget* desc=label((pkg.id+"\n"+pkg.url).c_str());
        gtk_widget_set_hexpand(desc,TRUE);gtk_label_set_wrap(GTK_LABEL(desc),TRUE);
        gtk_box_append(GTK_BOX(line),desc);
        if(pkg.platform==pgr_pack_manager::platform()&&!manager.find(pkg.id)) {
            pack_button(ui,line,"Скачать и установить","install",pkg.id);
            pack_button(ui,line,"Из ZIP…","import",pkg.id);
        } else gtk_box_append(GTK_BOX(line),label("Не для этой платформы или уже установлен"));
        gtk_box_append(GTK_BOX(body),line);
        gtk_box_append(GTK_BOX(body),gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    }
    gtk_box_append(GTK_BOX(body),label("GCC и Clang устанавливаются одним проверенным комплектом WinLibs UCRT x64. Для ARM/RISC-V и MSVC используйте отдельные дистрибутивы и штатные установщики; они не входят в стандартный набор."));
    gtk_window_present(GTK_WINDOW(window));
}
static void run_action(GtkButton*, gpointer payload) {
    // Callback dispatch passes its selected action id in a callback-specific context.
    auto* pair = static_cast<std::pair<Gui*, std::string>*>(payload);
    auto* ui = pair->first;
    const std::string& id = pair->second;
    if (id == "exit") {
        gtk_window_close(GTK_WINDOW(ui->window)); return;
    }
    if (id == "new") { open_create_dialog(ui); return; }
    if (id == "discard") {
        if (!ui->busy && !ui->selected.name.empty()) load_project(ui,ui->selected);
        return;
    }
    if (id == "exit-discard") { gtk_window_close(GTK_WINDOW(ui->window)); return; }
    if (id == "pack-check") {open_pack_manager(ui);return;}
    if (id == "pack-manager") {open_pack_manager(ui);return;}
    if (id == "refresh") {
        if (!ui->dirty || ui->selected.name.empty() || save_settings(ui)) refresh_projects(ui);
        return;
    }
    if (id == "switch") { set_status(ui, "Выберите проект в правой панели"); return; }
    if (id=="project-rename" || id=="project-relocate" || id=="project-archive" || id=="project-clone") {
        open_project_operation(ui,id.substr(8));return;
    }
    if (id == "pack") { open_pack_dialog(ui); return; }
    if (id == "profile-save") { open_save_profile(ui); return; }
    if (id == "profile-load") { open_load_profile(ui); return; }
    if (id == "diagnostics") { show_diagnostics(ui); gtk_notebook_set_current_page(GTK_NOTEBOOK(ui->results), 2); return; }
    if (id == "reload") { if (ui->dirty) {set_status(ui,"Не сохранено: сохраните либо переключите проект с выбором «Без сохранения»");return;} if (!ui->selected.name.empty()) load_project(ui, ui->selected); return; }
    if (id == "save") { save_settings(ui); return; }
    if (id == "stop") {
        if (ui->busy) { ui->cancellation.cancel(); set_status(ui, "Останавливаю дерево процессов…"); }
        return;
    }
    if (ui->busy || !save_settings(ui)) return;
    if (id == "clean") { confirm_clean(nullptr, ui); return; }
    if (id == "build") {ui->running_mode=pgr_gui_ux::build_mode::generated;run_cli(ui, {"--ci"}, "CMake-сборка");}
    else if (id == "direct") {ui->running_mode=pgr_gui_ux::build_mode::direct;run_cli(ui, {"--ci", "--direct"}, "Прямая компиляция");}
    else if (id == "native") {ui->running_mode=pgr_gui_ux::build_mode::native;run_cli(ui, {"--ci", "--native-cmake"}, "Существующий CMakeLists.txt");}
    else if (id == "generate") run_cli(ui, {"--generate-only"}, "Генерация CMakeLists.txt");
    else if (id == "command") run_cli(ui, {"--show-command"}, "Команда компиляции");
    else if (id == "export") run_cli(ui, {"--export-commands"}, "Экспорт compile_commands.json");
    else if (id == "analyze") run_cli(ui, {"--analyze"}, "Анализ clangd");
    else if (id == "presets") run_cli(ui, {"--write-presets"}, "Генерация CMakePresets.json");
}
static void release_action(gpointer ptr, GClosure*) {
    delete static_cast<std::pair<Gui*, std::string>*>(ptr);
}
static GtkWidget* action(Gui* ui, GtkWidget* container, const char* title, const char* id) {
    GtkWidget* button = gtk_button_new_with_label(title);
    gtk_widget_set_hexpand(button, TRUE);
    gtk_box_append(GTK_BOX(container), button);
    auto* ctx = new std::pair<Gui*, std::string>(ui, id);
    g_signal_connect_data(button, "clicked", G_CALLBACK(run_action), ctx, release_action, G_CONNECT_DEFAULT);
    if (std::string(id) != "new" && std::string(id) != "refresh" && std::string(id) != "exit" &&
        std::string(id) != "stop") {
        // The configuration panel is disabled during builds, including its actions.
    }
    return button;
}
// One closure path for the window controls and menu action. Never discard
// modified compiler settings without an explicit decision from the user.
struct CloseDialogState { Gui* ui; GtkWidget* dialog; };
static void close_dialog_destroy(gpointer payload) {
    auto* state=static_cast<CloseDialogState*>(payload);
    state->ui->close_dialog_open=false;
    delete state;
}
static void close_with_layout(Gui* ui) {
    if(ui->layout_save_source) {
        g_source_remove(ui->layout_save_source);
        ui->layout_save_source=0;
    }
    save_layout_now(ui);
    gtk_window_destroy(GTK_WINDOW(ui->window));
}
static void close_dialog_button(GtkButton* button,gpointer payload) {
    auto* state=static_cast<CloseDialogState*>(payload);
    Gui* ui=state->ui;
    const char* kind=static_cast<const char*>(g_object_get_data(G_OBJECT(button),"close-kind"));
    const std::string decision=kind?kind:"cancel";
    if(decision=="save" && !save_settings(ui)) return;
    const bool finish=(decision=="save"||decision=="discard");
    gtk_window_destroy(GTK_WINDOW(state->dialog));
    if(finish) close_with_layout(ui);
}
static gboolean on_close_request(GtkWindow*,gpointer payload) {
    auto* ui=static_cast<Gui*>(payload);
    if (ui->busy) {
        ui->cancellation.cancel();
        set_status(ui,"Останавливаю сборку. Закройте окно после завершения.");
        return TRUE;
    }
    if (ui->dirty) {
        if(ui->close_dialog_open) return TRUE;
        ui->close_dialog_open=true;
        GtkWidget* dialog=gtk_window_new();
        auto* state=new CloseDialogState{ui,dialog};
        g_object_set_data_full(G_OBJECT(dialog),"pgr-close",state,close_dialog_destroy);
        gtk_window_set_title(GTK_WINDOW(dialog),"Несохранённые изменения");
        gtk_window_set_modal(GTK_WINDOW(dialog),TRUE);
        gtk_window_set_transient_for(GTK_WINDOW(dialog),GTK_WINDOW(ui->window));
        gtk_window_set_default_size(GTK_WINDOW(dialog),420,150);
        GtkWidget* body=column(12);margins(body,18);
        gtk_window_set_child(GTK_WINDOW(dialog),body);
        auto* prompt=label("Сохранить изменения конфигурации перед выходом?");
        gtk_label_set_wrap(GTK_LABEL(prompt),TRUE);
        gtk_box_append(GTK_BOX(body),prompt);
        GtkWidget* buttons=row(8);
        for(auto& item:std::vector<std::pair<const char*,const char*>>{
            {"Сохранить","save"},{"Не сохранять","discard"},{"Отмена","cancel"}}) {
            auto* button=gtk_button_new_with_label(item.first);
            g_object_set_data(G_OBJECT(button),"close-kind",const_cast<char*>(item.second));
            g_signal_connect(button,"clicked",G_CALLBACK(close_dialog_button),state);
            gtk_box_append(GTK_BOX(buttons),button);
        }
        gtk_box_append(GTK_BOX(body),buttons);
        gtk_window_present(GTK_WINDOW(dialog));
        return TRUE;
    }
    if (ui->layout_save_source) {g_source_remove(ui->layout_save_source);ui->layout_save_source=0;}
    save_layout_now(ui);
    return FALSE;
}

static void open_build_folder(GtkButton*,gpointer payload) {
    auto* ui=static_cast<Gui*>(payload);
    if(!fs::is_directory(ui->output_directory)) {set_status(ui,"Каталог сборки ещё не создан");return;}
    GFile* file=g_file_new_for_path(path_utf8(ui->output_directory).c_str());
    gchar* uri=g_file_get_uri(file);
    GError* error=nullptr;
    if(!g_app_info_launch_default_for_uri(uri,nullptr,&error)) {
        set_status(ui,std::string("Не удалось открыть папку: ")+(error?error->message:"unknown"));
        g_clear_error(&error);
    }
    g_free(uri);g_object_unref(file);
}
static void run_built_executable(GtkButton*,gpointer payload) {
    auto* ui=static_cast<Gui*>(payload);
    if(ui->last_artifact.empty() || !pgr_gui_ux::is_native_executable(ui->last_artifact)) return;
    const auto exe=path_utf8(ui->last_artifact);
    const char* argv[]={exe.c_str(),nullptr};
    GError* error=nullptr;
    GSubprocess* child=g_subprocess_newv(argv,G_SUBPROCESS_FLAGS_NONE,&error);
    if(child) {g_object_unref(child);set_status(ui,"Запущен результат: "+exe);}
    else {set_status(ui,std::string("Не удалось запустить: ")+(error?error->message:"unknown"));g_clear_error(&error);}
}
static void activate(GtkApplication* app, gpointer payload) {
    auto* ui = static_cast<Gui*>(payload);
    ui->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(ui->window), "PGR_Make · Build Workspace");
    gtk_window_set_default_size(GTK_WINDOW(ui->window),
                                ui->layout.window_width, ui->layout.window_height);
    // Keep the platform's native CSD controls; CSS rules for application buttons
    // do not apply to system minimize/maximize/close buttons.
    GtkEventController* keyboard = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keyboard, GTK_PHASE_CAPTURE);
    g_signal_connect(keyboard, "key-pressed", G_CALLBACK(on_global_key), ui);
    gtk_widget_add_controller(ui->window, keyboard);
    gtk_widget_add_css_class(ui->window, "pgr-window");
    ui->theme_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(ui->theme_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    apply_theme(ui);
    g_signal_connect(ui->window,"close-request",G_CALLBACK(on_close_request),ui);

    GtkWidget* root = column(0);
    gtk_widget_add_css_class(root, "pgr-app");
    gtk_window_set_child(GTK_WINDOW(ui->window), root);

    GtkWidget* header = row(14);
    gtk_widget_add_css_class(header, "pgr-header");
    gtk_box_append(GTK_BOX(root), header);
    GtkWidget* brand_icon = label("◆");
    gtk_widget_add_css_class(brand_icon, "pgr-brand-mark");
    gtk_box_append(GTK_BOX(header), brand_icon);
    GtkWidget* brand_text = column(2);
    GtkWidget* title = label("PGR_Make");
    gtk_widget_add_css_class(title, "pgr-brand");
    gtk_box_append(GTK_BOX(brand_text), title);
    GtkWidget* subtitle = label("Проекты · Инструменты · Сборка");
    gtk_widget_add_css_class(subtitle, "pgr-subtitle");
    gtk_box_append(GTK_BOX(brand_text), subtitle);
    gtk_box_append(GTK_BOX(header), brand_text);
    GtkWidget* version = label(PGR_APP_VERSION " · GTK4");
    gtk_widget_add_css_class(version, "pgr-pill");
    gtk_box_append(GTK_BOX(header), version);
    GtkWidget* header_gap = column();
    gtk_widget_set_hexpand(header_gap, TRUE);
    gtk_box_append(GTK_BOX(header), header_gap);
    ui->active_project_label = label("Нет проекта");
    gtk_widget_add_css_class(ui->active_project_label, "pgr-meta");
    gtk_box_append(GTK_BOX(header), ui->active_project_label);
    ui->dirty_indicator = label("✓ Сохранено");
    gtk_widget_add_css_class(ui->dirty_indicator, "pgr-save-state");
    gtk_box_append(GTK_BOX(header), ui->dirty_indicator);
    ui->theme_button = gtk_button_new_with_label("☀ Светлая");
    gtk_widget_set_tooltip_text(ui->theme_button, "Переключить тёмную и светлую темы");
    g_signal_connect(ui->theme_button,"clicked",G_CALLBACK(toggle_theme),ui);
    gtk_box_append(GTK_BOX(header), ui->theme_button);

    // Required orientation: LEFT contains all TUI settings/commands;
    // RIGHT contains the selectable project list.
    // Nested GtkPaned: horizontal configuration/projects above a full-width,
    // vertically resizable output area. Neither splitter depends on project state.
    GtkWidget* vertical = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    ui->vertical_paned = vertical;
    gtk_widget_set_hexpand(vertical, TRUE);
    gtk_widget_set_vexpand(vertical, TRUE);
    gtk_paned_set_wide_handle(GTK_PANED(vertical), TRUE);
    gtk_widget_add_css_class(vertical, "pgr-vertical-paned");
    gtk_box_append(GTK_BOX(root), vertical);
    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    ui->horizontal_paned = paned;
    gtk_widget_set_hexpand(paned, TRUE);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_paned_set_wide_handle(GTK_PANED(paned), TRUE);
    gtk_paned_set_position(GTK_PANED(paned), ui->layout.projects_split);
    gtk_widget_add_css_class(paned, "pgr-workspace");
    gtk_paned_set_start_child(GTK_PANED(vertical), paned);
    gtk_paned_set_position(GTK_PANED(vertical), ui->layout.output_split);
    g_signal_connect(vertical, "notify::position", G_CALLBACK(on_split_position), ui);
    g_signal_connect(paned, "notify::position", G_CALLBACK(on_split_position), ui);

    GtkWidget* left = column(12);
    gtk_widget_add_css_class(left, "pgr-main");
    gtk_widget_set_size_request(left, 620, -1);
    gtk_widget_set_margin_end(left, 12);
    gtk_paned_set_start_child(GTK_PANED(paned), left);
    // Compact primary toolbar. Every advanced command remains in the TUI actions expander.
    GtkWidget* top_commands = row(9);
    gtk_widget_add_css_class(top_commands, "pgr-toolbar");
    GtkWidget* build_caption = label("Режим:");
    gtk_widget_add_css_class(build_caption, "pgr-meta");
    gtk_box_append(GTK_BOX(top_commands), build_caption);
    const char* modes[] = {"PGR CMake", "Существующий CMake", "Прямая сборка", nullptr};
    ui->build_choice = gtk_drop_down_new_from_strings(modes);
    gtk_widget_set_tooltip_text(ui->build_choice, "Способ сборки проекта");
    gtk_widget_set_hexpand(ui->build_choice, TRUE);
    gtk_box_append(GTK_BOX(top_commands), ui->build_choice);
    g_signal_connect(ui->build_choice,"notify::selected",G_CALLBACK(on_mode_changed),ui);
    GtkWidget* primary = gtk_button_new_with_label("▶ Собрать");
    gtk_widget_add_css_class(primary, "suggested-action");
    gtk_widget_add_css_class(primary, "pgr-build");
    g_signal_connect(primary, "clicked", G_CALLBACK(primary_build), ui);
    gtk_box_append(GTK_BOX(top_commands), primary);
    ui->quick_actions.push_back(primary);
    ui->quick_actions.push_back(action(ui, top_commands, "Сохранить", "save"));
    ui->stop_button = action(ui, top_commands, "■ Стоп", "stop");
    gtk_widget_add_css_class(ui->stop_button, "pgr-stop");
    GtkWidget* toggle = gtk_button_new_with_label(ui->layout.output_visible ? "▾ Вывод" : "▸ Вывод");
    ui->output_toggle = toggle;
    gtk_widget_set_tooltip_text(toggle, "Показать/скрыть нижнюю панель (Ctrl+J)");
    g_signal_connect(toggle, "clicked", G_CALLBACK(toggle_output), ui);
    gtk_box_append(GTK_BOX(top_commands), toggle);
    // Global toolbar is independent of the two workspace panes.
    gtk_box_insert_child_after(GTK_BOX(root), top_commands,
                               gtk_widget_get_first_child(root));
    GtkWidget* summary=row(16);
    gtk_widget_add_css_class(summary,"pgr-project-summary");
    auto summary_item=[&](const char* caption,GtkWidget** output) {
        auto* cell=column(3);
        auto* heading=label(caption);
        gtk_widget_add_css_class(heading,"pgr-meta");
        gtk_box_append(GTK_BOX(cell),heading);
        *output=label("—");
        gtk_label_set_ellipsize(GTK_LABEL(*output),PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(*output),28);
        gtk_widget_add_css_class(*output,"pgr-summary-value");
        gtk_box_append(GTK_BOX(cell),*output);
        gtk_widget_set_hexpand(cell,TRUE);
        gtk_box_append(GTK_BOX(summary),cell);
    };
    summary_item("КОМПИЛЯТОР",&ui->summary_compiler);
    summary_item("CMAKE",&ui->summary_cmake);
    summary_item("РЕЖИМ",&ui->summary_mode);
    summary_item("ПРОФИЛЬ",&ui->summary_profile);
    gtk_box_insert_child_after(GTK_BOX(root),summary,top_commands);

    GtkWidget* controls = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(controls, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(controls), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    ui->left_controls = controls;
    GtkWidget* form = column(8);
    margins(form, 8);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(controls), form);
    GtkWidget* settings_header = row(8);
    const char* groups[] = {"Все настройки", "Основные", "Флаги", "Пути", "CMake", "Инструменты", "Команды", nullptr};
    ui->category_select = gtk_drop_down_new_from_strings(groups);
    gtk_widget_set_size_request(ui->category_select, 165, -1);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(ui->category_select), static_cast<guint>(ui->layout.category));
    gtk_box_append(GTK_BOX(settings_header), ui->category_select);
    ui->settings_search = gtk_search_entry_new();
    gtk_widget_set_hexpand(ui->settings_search, TRUE);
    g_object_set(G_OBJECT(ui->settings_search), "placeholder-text", "Поиск настроек…", nullptr);
    gtk_box_append(GTK_BOX(settings_header), ui->settings_search);
    gtk_box_append(GTK_BOX(left), settings_header);
    GtkWidget* validate_row=row(8);
    ui->validation_label=label("Проверка конфигурации ещё не выполнялась");
    gtk_widget_add_css_class(ui->validation_label,"pgr-validation-label");
    gtk_widget_set_hexpand(ui->validation_label,TRUE);
    gtk_label_set_wrap(GTK_LABEL(ui->validation_label),TRUE);
    gtk_box_append(GTK_BOX(validate_row),ui->validation_label);
    ui->validation_action=gtk_button_new_with_label("Проверить");
    g_signal_connect(ui->validation_action,"clicked",G_CALLBACK(check_button_clicked),ui);
    gtk_box_append(GTK_BOX(validate_row),ui->validation_action);
    ui->install_missing_button=gtk_button_new_with_label("Найти инструмент в Pack");
    gtk_widget_set_visible(ui->install_missing_button,FALSE);
    g_signal_connect(ui->install_missing_button,"clicked",G_CALLBACK(install_missing_clicked),ui);
    gtk_box_append(GTK_BOX(validate_row),ui->install_missing_button);
    gtk_box_append(GTK_BOX(left),validate_row);
    g_signal_connect(ui->settings_search, "changed", G_CALLBACK(settings_search_changed), ui);
    g_signal_connect(ui->category_select, "notify::selected", G_CALLBACK(category_changed), ui);
    gtk_box_append(GTK_BOX(left), controls);
    ui->settings_form=form;
    build_configuration_form(ui, form);
    observe_changes(ui);
    g_signal_connect(ui->singles.at("profile").dropdown, "notify::selected",
                     G_CALLBACK(on_profile_changed), ui);

    GtkWidget* output = column(7);
    ui->output_panel = output;
    gtk_widget_set_size_request(output, -1, 145);
    gtk_widget_add_css_class(output, "pgr-output-panel");
    GtkWidget* output_toolbar = row(10);
    gtk_widget_add_css_class(output_toolbar, "pgr-output-toolbar");
    GtkWidget* output_title = label("РЕЗУЛЬТАТЫ СБОРКИ");
    gtk_widget_add_css_class(output_title, "pgr-panel-title");
    gtk_box_append(GTK_BOX(output_toolbar), output_title);
    ui->log_search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->log_search), "Поиск в журнале · Ctrl+F");
    gtk_widget_set_hexpand(ui->log_search, TRUE);
    gtk_box_append(GTK_BOX(output_toolbar), ui->log_search);
    ui->search_status = label("");
    gtk_widget_add_css_class(ui->search_status, "pgr-meta");
    gtk_box_append(GTK_BOX(output_toolbar), ui->search_status);
    ui->autoscroll_button = gtk_check_button_new_with_label("Автопрокрутка");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->autoscroll_button), ui->layout.autoscroll);
    g_signal_connect(ui->autoscroll_button, "toggled", G_CALLBACK(on_autoscroll), ui);
    gtk_box_append(GTK_BOX(output_toolbar), ui->autoscroll_button);
    GtkWidget* copy_all = gtk_button_new_with_label("Копировать");
    g_signal_connect(copy_all, "clicked", G_CALLBACK(copy_log), ui);
    gtk_box_append(GTK_BOX(output_toolbar), copy_all);
    GtkWidget* clear = gtk_button_new_with_label("Очистить");
    gtk_widget_set_tooltip_text(clear, "Очистить только видимое содержимое журнала");
    g_signal_connect(clear, "clicked", G_CALLBACK(clear_log), ui);
    gtk_box_append(GTK_BOX(output_toolbar), clear);
    ui->output_expand = gtk_button_new_with_label("Развернуть");
    g_signal_connect(ui->output_expand, "clicked", G_CALLBACK(expand_output), ui);
    gtk_box_append(GTK_BOX(output_toolbar), ui->output_expand);
    gtk_box_append(GTK_BOX(output), output_toolbar);
    ui->results = gtk_notebook_new();
    gtk_widget_set_vexpand(ui->results, TRUE);
    gtk_widget_add_css_class(ui->results, "pgr-results");
    g_signal_connect(ui->results, "switch-page", G_CALLBACK(on_output_page), ui);
    GtkWidget* result_page=column(12);
    margins(result_page,15);
    gtk_widget_add_css_class(result_page,"pgr-result-card");
    ui->result_status=label("Проект готов к сборке");
    gtk_widget_add_css_class(ui->result_status,"pgr-result-title");
    gtk_box_append(GTK_BOX(result_page),ui->result_status);
    ui->result_detail=label("Выберите режим и нажмите «Собрать».");
    gtk_label_set_wrap(GTK_LABEL(ui->result_detail),TRUE);
    gtk_box_append(GTK_BOX(result_page),ui->result_detail);
    GtkWidget* result_actions=row(10);
    ui->open_folder_button=gtk_button_new_with_label("Открыть каталог сборки");
    gtk_widget_set_sensitive(ui->open_folder_button,FALSE);
    gtk_box_append(GTK_BOX(result_actions),ui->open_folder_button);
    ui->run_artifact_button=gtk_button_new_with_label("Запустить EXE");
    gtk_widget_set_sensitive(ui->run_artifact_button,FALSE);
    gtk_box_append(GTK_BOX(result_actions),ui->run_artifact_button);
    g_signal_connect(ui->open_folder_button,"clicked",G_CALLBACK(open_build_folder),ui);
    g_signal_connect(ui->run_artifact_button,"clicked",G_CALLBACK(run_built_executable),ui);
    gtk_box_append(GTK_BOX(result_page),result_actions);
    gtk_notebook_append_page(GTK_NOTEBOOK(ui->results),result_page,gtk_label_new("Результат"));
    ui->log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ui->log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(ui->log_view), TRUE);
    gtk_widget_add_css_class(ui->log_view, "pgr-log");
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ui->log_view), GTK_WRAP_WORD_CHAR);
    ui->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ui->log_view));
    ui->search_tag = gtk_text_buffer_create_tag(ui->log_buffer, "pgr-match-highlight",
                                                 "background", "#D97757",
                                                 "foreground", "#141413", nullptr);
    g_signal_connect(ui->log_search, "changed", G_CALLBACK(on_log_search), ui);
    GtkWidget* logs_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(logs_scroll), ui->log_view);
    gtk_widget_add_css_class(logs_scroll, "pgr-log-area");
    gtk_notebook_append_page(GTK_NOTEBOOK(ui->results), logs_scroll, gtk_label_new("Журнал"));
    GtkWidget* errors_scroll = gtk_scrolled_window_new();
    GtkWidget* errors = column(6);
    margins(errors, 6);
    ui->diags_count = label("Ошибок нет");
    gtk_box_append(GTK_BOX(errors), ui->diags_count);
    GtkWidget* diagnostic_controls=row(10);
    ui->severity_errors=gtk_check_button_new_with_label("Ошибки");
    ui->severity_warnings=gtk_check_button_new_with_label("Предупреждения");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->severity_errors),TRUE);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui->severity_warnings),TRUE);
    g_signal_connect(ui->severity_errors,"toggled",G_CALLBACK(diagnostic_filter_changed),ui);
    g_signal_connect(ui->severity_warnings,"toggled",G_CALLBACK(diagnostic_filter_changed),ui);
    gtk_box_append(GTK_BOX(diagnostic_controls),ui->severity_errors);
    gtk_box_append(GTK_BOX(diagnostic_controls),ui->severity_warnings);
    GtkWidget* copy=gtk_button_new_with_label("Копировать выбранное");
    g_signal_connect(copy,"clicked",G_CALLBACK(copy_diagnostic),ui);
    gtk_box_append(GTK_BOX(diagnostic_controls),copy);
    gtk_box_append(GTK_BOX(errors),diagnostic_controls);
    ui->diagnostics_box = gtk_list_box_new();
    gtk_widget_add_css_class(ui->diagnostics_box, "pgr-diagnostic-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui->diagnostics_box),GTK_SELECTION_SINGLE);
    g_signal_connect(ui->diagnostics_box, "row-activated", G_CALLBACK(on_diagnostic_activated), ui);
    gtk_box_append(GTK_BOX(errors), ui->diagnostics_box);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(errors_scroll), errors);
    gtk_notebook_append_page(GTK_NOTEBOOK(ui->results), errors_scroll, gtk_label_new("Диагностика"));
    GtkWidget* history_scroller=gtk_scrolled_window_new();
    ui->history_box=gtk_list_box_new();
    gtk_widget_add_css_class(ui->history_box,"pgr-history-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui->history_box),GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(history_scroller),ui->history_box);
    gtk_notebook_append_page(GTK_NOTEBOOK(ui->results),history_scroller,gtk_label_new("История"));
    gtk_box_append(GTK_BOX(output), ui->results);
    gtk_paned_set_end_child(GTK_PANED(vertical), output);
    gtk_widget_set_visible(output, ui->layout.output_visible);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(ui->results), ui->layout.output_page);

    GtkWidget* right = column(10);
    gtk_widget_add_css_class(right, "pgr-sidebar");
    gtk_widget_set_size_request(right, 278, -1);
    gtk_paned_set_end_child(GTK_PANED(paned), right);
    GtkWidget* title_projects = label("ПРОЕКТЫ");
    gtk_widget_add_css_class(title_projects, "pgr-sidebar-heading");
    gtk_box_append(GTK_BOX(right), title_projects);
    GtkWidget* buttons = row(5);
    GtkWidget* create = action(ui, buttons, "+ Создать", "new");
    GtkWidget* refresh = action(ui, buttons, "Обновить", "refresh");
    ui->project_actions.push_back(create);
    ui->project_actions.push_back(refresh);
    gtk_box_append(GTK_BOX(right), buttons);
    ui->project_search=gtk_search_entry_new();
    g_object_set(G_OBJECT(ui->project_search),"placeholder-text","Поиск по проектам…",nullptr);
    gtk_box_append(GTK_BOX(right),ui->project_search);
    GtkWidget* projects_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(projects_scroll, TRUE);
    ui->project_list = gtk_list_box_new();
    gtk_widget_add_css_class(ui->project_list, "pgr-project-list");
    gtk_list_box_set_filter_func(GTK_LIST_BOX(ui->project_list),filter_project,ui,nullptr);
    g_signal_connect(ui->project_search,"changed",G_CALLBACK(search_projects_changed),ui);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(ui->project_list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(ui->project_list), TRUE);
    g_signal_connect(ui->project_list, "row-activated", G_CALLBACK(project_activated), ui);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(projects_scroll), ui->project_list);
    gtk_box_append(GTK_BOX(right), projects_scroll);
    gtk_box_append(GTK_BOX(right), label("Каталог исходников:"));
    ui->project_path = label("Не выбран");
    gtk_label_set_wrap(GTK_LABEL(ui->project_path), TRUE);
    gtk_label_set_selectable(GTK_LABEL(ui->project_path), TRUE);
    gtk_box_append(GTK_BOX(right), ui->project_path);
    GtkWidget* manage_project = gtk_expander_new("Действия с проектом");
    gtk_widget_add_css_class(manage_project, "pgr-section");
    GtkWidget* manage_actions = column(6);
    margins(manage_actions, 8);
    action(ui, manage_actions, "Переименовать", "project-rename");
    action(ui, manage_actions, "Клонировать настройки", "project-clone");
    action(ui, manage_actions, "Изменить путь исходников", "project-relocate");
    action(ui, manage_actions, "Убрать из списка", "project-archive");
    gtk_expander_set_child(GTK_EXPANDER(manage_project), manage_actions);
    gtk_box_append(GTK_BOX(right), manage_project);
    action(ui, right, "◈ Управление Pack", "pack-check");
    GtkWidget* manage_app = gtk_expander_new("Приложение");
    gtk_widget_add_css_class(manage_app, "pgr-section");
    GtkWidget* app_actions = column(6);
    margins(app_actions, 8);
    action(ui,app_actions,"Выход","exit");

    gtk_expander_set_child(GTK_EXPANDER(manage_app), app_actions);
    gtk_box_append(GTK_BOX(right), manage_app);

    GtkWidget* footer = row(8);
    gtk_widget_add_css_class(footer, "pgr-statusbar");
    GtkWidget* status_indicator = label("●");
    gtk_widget_add_css_class(status_indicator, "pgr-accent");
    gtk_box_append(GTK_BOX(footer), status_indicator);
    ui->status_spinner = gtk_spinner_new();
    gtk_widget_set_visible(ui->status_spinner, FALSE);
    gtk_box_append(GTK_BOX(footer), ui->status_spinner);
    ui->status = label("Готово");
    gtk_label_set_ellipsize(GTK_LABEL(ui->status),PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(ui->status, "pgr-meta");
    gtk_widget_set_hexpand(ui->status, TRUE);
    gtk_box_append(GTK_BOX(footer), ui->status);
    gtk_box_append(GTK_BOX(footer), label("PGR_Make " PGR_APP_VERSION " · GTK4"));
    gtk_box_append(GTK_BOX(root), footer);
    update_settings_filter(ui);
    refresh_projects(ui);
    log_line(ui, "PGR_Make GTK4: рабочая область и журнал разделены двумя GtkPaned.");
    gtk_window_present(GTK_WINDOW(ui->window));
    g_idle_add(restore_split_ratios,ui);
}
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    Gui ui(pgr_workspace::executable_directory());
    std::string error;
    if (!ui.registry.ensure(error)) {
        g_printerr("Cannot initialize PGR workspace: %s\n", error.c_str()); return 1;
    }
    GtkApplication* app = gtk_application_new("com.pgr.make", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &ui);
    const int result = g_application_run(G_APPLICATION(app), 0, nullptr);
    if (ui.layout_save_source) {
        g_source_remove(ui.layout_save_source);
        ui.layout_save_source = 0;
    }
    ui.cancellation.cancel();
    if (ui.worker.joinable()) ui.worker.join();
    if (ui.theme_provider) {
        if (GdkDisplay* display = gdk_display_get_default())
            gtk_style_context_remove_provider_for_display(display, GTK_STYLE_PROVIDER(ui.theme_provider));
        g_object_unref(ui.theme_provider);
    }
    g_object_unref(app);
    return result;
}
