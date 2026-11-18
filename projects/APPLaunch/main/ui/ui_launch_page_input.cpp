/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_launch_page.h"

#include "hal_lvgl_bsp.h"
#include "input_keys.h"
#include "keyboard_input.h"
#include "model/screensaver_model.hpp"
#include "sample_log.h"

#include <cctype>

#undef SLOGI
#define SLOGI(...) do { } while (0)

namespace {

// ============================================================
// audio
// ============================================================

static void audio_play_switch(void)
{
    cp0_signal_audio_api({"SystemSoundPlay", "1"}, nullptr);
}

static void audio_play_enter(void)
{
    cp0_signal_audio_api({"SystemSoundPlay", "2"}, nullptr);
}

static uint32_t fzxc_to_arrow(uint32_t key)
{
    switch (key)
    {
    case KEY_F:
        return KEY_UP;

    case KEY_X:
        return KEY_DOWN;

    case KEY_Z:
        return KEY_LEFT;

    case KEY_C:
        return KEY_RIGHT;

    default:
        return key;
    }
}

static UILaunchPage *page_from_event(lv_event_t *event)
{
    return event ? static_cast<UILaunchPage *>(lv_event_get_user_data(event)) : nullptr;
}

} // namespace

void UILaunchPage::handle_home_key(lv_event_t *event)
{
    if (!event)
        return;

    struct key_item *elm = static_cast<struct key_item *>(lv_event_get_param(event));
    if (!elm)
        return;

    /* Text editors own printable keys, including F/X/Z/C.  This callback is
     * installed on the active screen, so it must not let those events leak
     * into launcher navigation while an editor has selected text context. */
    if (elm->input_context == KBD_INPUT_CONTEXT_TEXT ||
        cp0_keyboard_get_input_context() == KBD_INPUT_CONTEXT_TEXT) {
        return;
    }

    uint32_t code = elm->semantic_key ? elm->semantic_key : fzxc_to_arrow(elm->key_code);

    SLOGI("[LAUNCHER] main_key_switch raw=%u->code=%u state=%s sym=%s",
           elm->key_code,
           code,
           kbd_state_name(elm->key_state),
           elm->sym_name);

    if (list_mode_ && list_container_) {
        handle_list_key(elm, code);
        return;
    }

    if (elm->key_state)
    {
        switch (code)
        {
        case KEY_UP:
            break;

        case KEY_DOWN:
            break;

        case KEY_LEFT:
        {
            if (navigation_.keyboard_navigation_enabled())
            {
                audio_play_switch();
                switch_right();
            }
        }
        break;

        case KEY_RIGHT:
        {
            if (navigation_.keyboard_navigation_enabled())
            {
                audio_play_switch();
                switch_left();
            }
        }
        break;

        default:
            break;
        }
    }
    else if (code == KEY_ENTER)
    {
        audio_play_enter();
        launch_selected_app();
    }
}

// Keys are owned by the Tab menu while it is open: nothing reaches the list or launches an app.
// Closing happens on press (Esc/Tab), picking on Enter release, so the matching release does nothing.
void UILaunchPage::handle_list_menu_key(const struct key_item *elm, uint32_t code)
{
    if (!elm->key_state) {
        if (code == KEY_ENTER && list_menu_sel_ < list_menu_items_.size()) {
            audio_play_enter();
            activate_list_menu(list_menu_items_[list_menu_sel_]);
        }
        return;
    }
    const size_t count = list_menu_items_.size();
    if (code == KEY_ESC || code == KEY_TAB) {
        audio_play_switch();
        close_list_menu();
    } else if ((code == KEY_UP || code == KEY_DOWN) && count > 1) {
        list_menu_sel_ = (list_menu_sel_ + (code == KEY_DOWN ? 1 : count - 1)) % count;
        audio_play_switch();
        update_list_menu();
    }
}

void UILaunchPage::handle_list_key(const struct key_item *elm, uint32_t code)
{
    if (list_menu_) {
        list_tab_armed_ = false;
        handle_list_menu_key(elm, code);
        return;
    }
    if (code == KEY_TAB) {
        // A short Tab press (released before the lock-hold hint appears) opens the menu;
        // a longer hold is the screensaver gesture, which this page must not also act on.
        if (elm->key_state == KBD_KEY_PRESSED) {
            list_tab_armed_ = true;
            list_tab_down_tick_ = lv_tick_get();
        } else if (!elm->key_state) {
            const bool short_press =
                list_tab_armed_ && lv_tick_elaps(list_tab_down_tick_) < ScreensaverModel::hold_hint_ms();
            list_tab_armed_ = false;
            if (short_press) {
                audio_play_switch();
                open_list_menu();
            }
        }
        return;
    }
    if (elm->key_state) list_tab_armed_ = false;  // Tab chorded with another key
    if (!elm->key_state) {
        if (code == KEY_ENTER) {
            audio_play_enter();
            launch_selected_app();
        }
        return;
    }

    bool changed = false;
    switch (code) {
    case KEY_UP:    changed = list_move(-1, true); break;
    case KEY_DOWN:  changed = list_move(1, true); break;
    case KEY_LEFT:  changed = list_move(-static_cast<int>(kListRows), false); break;
    case KEY_RIGHT: changed = list_move(static_cast<int>(kListRows), false); break;
    default: {
        // F/X/Z/C are arrows; fn/ctrl/alt/logo combinations are shortcuts, not text.
        const uint32_t blocked = KBD_MOD_CTRL | KBD_MOD_ALT | KBD_MOD_LOGO | KBD_MOD_FN;
        if (elm->key_state != KBD_KEY_PRESSED || (elm->mods & blocked) ||
            fzxc_to_arrow(elm->key_code) != elm->key_code)
            return;
        const uint32_t c = elm->codepoint;
        if (c == ' ' || elm->key_code == KEY_SPACE)
            changed = list_cycle_favorite();
        else if (c < 0x80 && std::isalnum(static_cast<int>(c)))
            changed = list_jump(static_cast<char>(c));
        break;
    }
    }
    if (changed) audio_play_switch();
}

void UILaunchPage::on_left_arrow_clicked(lv_event_t *event) noexcept
{
    try {
        if (UILaunchPage *self = page_from_event(event)) self->switch_right();
    } catch (...) {
    }
}

void UILaunchPage::on_right_arrow_clicked(lv_event_t *event) noexcept
{
    try {
        if (UILaunchPage *self = page_from_event(event)) self->switch_left();
    } catch (...) {
    }
}

void UILaunchPage::on_app_clicked(lv_event_t *event) noexcept
{
    try {
        if (UILaunchPage *self = page_from_event(event)) self->launch_selected_app();
    } catch (...) {
    }
}

void UILaunchPage::on_home_key(lv_event_t *event) noexcept
{
    try {
        if (UILaunchPage *self = page_from_event(event)) self->handle_home_key(event);
    } catch (...) {
    }
}
