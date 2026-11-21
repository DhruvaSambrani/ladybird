/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashMap.h>
#include <LibCore/Resource.h>
#include <UI/Qt/Application.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/HubConnection.h>
#include <UI/Qt/StringUtils.h>
#include <UI/Qt/Tab.h>
#include <UI/Qt/VimKeybindings.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QTimer>
#include <QToolTip>

namespace Ladybird::VimKeybindings {

struct Context {
    BrowserWindow& window;
    Tab& tab;
};

using Action = void (*)(Context const&);

// The keys of a hint, which vim-hints.js labels the elements of the page with.
static constexpr int hint_length = 2;

static struct {
    // The start of a sequence of keys, which a binding continues.
    String pending_keys;
    // The keys of a hint that are still to come, which the hint script in the page takes.
    int keys_for_page { 0 };
    // Whether n and N go the other way, after a search started with ?.
    bool search_is_backward { false };
} s_state;

// Commands

static void run_script(Tab& tab, String const& script)
{
    HubConnection::the().async_view_run_javascript(tab.view().view_id(), script);
}

// The arrow keys scroll whatever the page would, such as the scroll container that has the focus.
static void press_key(Tab& tab, Qt::Key key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(&tab.view(), &press);
    QCoreApplication::sendEvent(&tab.view(), &release);
}

// The location field selects its text once it has the focus, which this must come after.
template<typename Callback>
static void after_location_focus(Tab& tab, Callback callback)
{
    QTimer::singleShot(0, &tab.location_edit(), [&location_edit = tab.location_edit(), callback] { callback(location_edit); });
}

static void edit_current_url(Context const& context)
{
    context.tab.focus_location_editor();
    after_location_focus(context.tab, [](LocationEdit& location_edit) { location_edit.end(false); });
}

static void edit_current_url_in_new_tab(Context const& context)
{
    auto url = qstring_from_ak_string(context.tab.url().serialize());
    context.window.open_new_tab();
    if (auto* new_tab = context.window.current_tab()) {
        // The URL goes in as though it were typed, so that the field edits it like any other text.
        after_location_focus(*new_tab, [url](LocationEdit& location_edit) {
            location_edit.insert(url);
            emit location_edit.textEdited(location_edit.text());
        });
    }
}

static void show_hints(Tab& tab, StringView mode)
{
    auto script = Core::Resource::load_from_uri("resource://ladybird/vim-hints.js"sv);
    if (script.is_error()) {
        warnln("Vim keybindings: Cannot load the hint script: {}", script.error());
        return;
    }
    s_state.keys_for_page = hint_length;
    run_script(tab, MUST(String::formatted("(() => {{ {}\nshowHints(\"{}\", {}); }})()", StringView { script.value()->data() }, mode, hint_length)));
}

static void copy_url(Tab& tab)
{
    auto url = qstring_from_ak_string(tab.url().serialize());
    QGuiApplication::clipboard()->setText(url);
    auto& location_edit = tab.location_edit();
    QToolTip::showText(location_edit.mapToGlobal(QPoint(0, location_edit.height())), "Copied " + url, &location_edit, {}, 1500);
}

// Opens the copied text as the location field would, as a URL or a search.
static void open_copied_text(Context const& context, bool in_new_tab)
{
    auto text = ak_string_from_qstring(QGuiApplication::clipboard()->text().trimmed());
    if (text.is_empty())
        return;
    auto& tab = in_new_tab ? context.window.create_new_tab(Web::HTML::ActivateTab::Yes, BrowserWindow::TabLocation::after_current_tab()) : context.tab;
    tab.view().load_from_user_input(text);
}

static constexpr struct {
    StringView keys;
    StringView name;
    Action action;
} s_commands[] = {
    { "h"sv, "scroll-left"sv, [](auto& context) { press_key(context.tab, Qt::Key_Left); } },
    { "j"sv, "scroll-down"sv, [](auto& context) { press_key(context.tab, Qt::Key_Down); } },
    { "k"sv, "scroll-up"sv, [](auto& context) { press_key(context.tab, Qt::Key_Up); } },
    { "l"sv, "scroll-right"sv, [](auto& context) { press_key(context.tab, Qt::Key_Right); } },
    { "H"sv, "back"sv, [](auto& context) { context.tab.view().traverse_the_history_by_delta(-1); } },
    { "L"sv, "forward"sv, [](auto& context) { context.tab.view().traverse_the_history_by_delta(1); } },
    { "r"sv, "reload"sv, [](auto& context) { context.tab.view().reload(); } },
    { "R"sv, "reload-bypassing-cache"sv, [](auto& context) { context.tab.view().reload(WebView::BypassCache::Yes); } },

    { "J"sv, "previous-tab"sv, [](auto& context) { context.window.open_previous_tab(); } },
    { "K"sv, "next-tab"sv, [](auto& context) { context.window.open_next_tab(); } },
    { "g0"sv, "first-tab"sv, [](auto& context) { context.window.activate_tab(0); } },
    { "g$"sv, "last-tab"sv, [](auto& context) { context.window.activate_tab(context.window.tab_count() - 1); } },
    { "b"sv, "tab-list"sv, [](auto& context) { context.window.show_all_tabs(); } },
    // The tab may go away at once, while its view is still handling this key.
    { "q"sv, "close-tab"sv, [](auto& context) { QTimer::singleShot(0, &context.tab, [&tab = context.tab] { tab.request_close(); }); } },

    { "/"sv, "find"sv, [](auto& context) { s_state.search_is_backward = false; context.tab.show_find_in_page(); } },
    { "?"sv, "find-backward"sv, [](auto& context) { s_state.search_is_backward = true; context.tab.show_find_in_page(); } },
    { "n"sv, "find-next"sv, [](auto& context) { s_state.search_is_backward ? context.tab.find_previous() : context.tab.find_next(); } },
    { "N"sv, "find-previous"sv, [](auto& context) { s_state.search_is_backward ? context.tab.find_next() : context.tab.find_previous(); } },

    { "o"sv, "open"sv, [](auto& context) { context.tab.focus_location_editor(); } },
    { "O"sv, "open-current"sv, edit_current_url },
    { "t"sv, "tab-open"sv, [](auto& context) { context.window.open_new_tab(); } },
    { "T"sv, "tab-open-current"sv, edit_current_url_in_new_tab },

    { "f"sv, "hint"sv, [](auto& context) { show_hints(context.tab, "open"sv); } },
    { "F"sv, "hint-new-tab"sv, [](auto& context) { show_hints(context.tab, "tab"sv); } },
    { "yf"sv, "hint-yank"sv, [](auto& context) { show_hints(context.tab, "yank"sv); } },

    { "yy"sv, "yank-url"sv, [](auto& context) { copy_url(context.tab); } },
    { "p"sv, "paste-open"sv, [](auto& context) { open_copied_text(context, false); } },
    { "P"sv, "paste-tab-open"sv, [](auto& context) { open_copied_text(context, true); } },
};

// Bindings

// The default bindings, with the changes of the setting, such as "x close-tab" or "x none".
static HashMap<String, Action> bindings()
{
    HashMap<String, Action> bindings;
    for (auto const& command : s_commands)
        bindings.set(MUST(String::from_utf8(command.keys)), command.action);

    auto const& changes = Application::settings().config_variable(WebView::ConfigVariableID::VimKeybindings);
    changes.as_array().for_each([&](JsonValue const& change) {
        auto parts = change.as_string().bytes_as_string_view().split_view(' ');
        if (parts.size() != 2)
            return;
        auto keys = MUST(String::from_utf8(parts[0]));
        if (parts[1] == "none"sv)
            bindings.remove(keys);
        for (auto const& command : s_commands) {
            if (parts[1] == command.name)
                bindings.set(keys, command.action);
        }
    });
    return bindings;
}

// Key handling

bool is_enabled()
{
    return Application::settings().config_variable_as_bool(WebView::ConfigVariableID::VimKeybindingsEnabled);
}

static bool pass_key_to_page(QKeyEvent const& event)
{
    if (event.key() == Qt::Key_Escape)
        s_state.keys_for_page = 0;
    else if (!event.text().isEmpty())
        --s_state.keys_for_page;
    return false;
}

// Keys typed into a text field are the field's, except Escape, which leaves the field.
static bool handle_key_in_text_field(Tab& tab, QKeyEvent const& event)
{
    if (event.key() != Qt::Key_Escape || event.modifiers() != Qt::NoModifier)
        return false;
    run_script(tab, "document.activeElement?.blur()"_string);
    return true;
}

// A key that continues no sequence of the pending keys is tried on its own, so that "y" and then "j" scrolls.
static bool handle_key_in_sequence(Context const& context, String const& key)
{
    auto pending_keys = exchange(s_state.pending_keys, {});
    auto bindings = VimKeybindings::bindings();
    for (auto const& keys : { MUST(String::formatted("{}{}", pending_keys, key)), key }) {
        if (auto action = bindings.get(keys); action.has_value()) {
            (*action)(context);
            return true;
        }
        for (auto const& binding : bindings) {
            if (binding.key.starts_with_bytes(keys)) {
                s_state.pending_keys = keys;
                return true;
            }
        }
    }
    return false;
}

bool handle_key_press(BrowserWindow& window, Tab& tab, QKeyEvent const& event)
{
    auto modifiers = event.modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (event.key() == Qt::Key_Escape && modifiers == Qt::ShiftModifier) {
        Application::settings().set_config_variable(WebView::ConfigVariableID::VimKeybindingsEnabled, !is_enabled());
        return true;
    }

    // Pressing Shift for the next key of a sequence leaves the sequence pending.
    if (!is_enabled() || event.key() == Qt::Key_Shift)
        return false;
    if (s_state.keys_for_page > 0)
        return pass_key_to_page(event);
    if (tab.view().input_method_state().is_enabled) {
        s_state.pending_keys = {};
        return handle_key_in_text_field(tab, event);
    }
    if ((modifiers & ~Qt::ShiftModifier) || event.text().size() != 1) {
        s_state.pending_keys = {};
        return false;
    }
    return handle_key_in_sequence({ window, tab }, ak_string_from_qstring(event.text()));
}

}
