/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_launch_page.h"

#include "launch.h"
#include "hal_lvgl_bsp.h"
#include "launcher_toast.h"
#include "launcher_platform.hpp"
#include "model/launcher_list_model.hpp"
#include "model/launcher_uninstall_model.hpp"
#include "app_registry.h"
#include "settings/settings_launcher_model.hpp"

#include <algorithm>
#include <cstdlib>
#include <climits>
#include <filesystem>
#include <fstream>
#include <list>
#include <sstream>
#include <thread>

namespace {

constexpr lv_coord_t kRowHeight = 24;
constexpr lv_coord_t kRowsWidth = 190;
constexpr lv_coord_t kCenterCard = 100;   // same sizes as the carousel, so the icon pool fits
constexpr lv_coord_t kSideCard = 80;
constexpr lv_coord_t kCardPitch = 80;     // centre-to-centre: about 20 px between the icons, a third of each neighbour shows
constexpr lv_coord_t kCardX = 100;        // from the container centre: card centre at x=260
constexpr int32_t kListAnimMs = 150;

lv_obj_t *create_list_row(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *row = lv_label_create(parent);
    if (!row) return nullptr;
    lv_obj_set_size(row, kRowsWidth, kRowHeight);
    lv_obj_set_pos(row, 4, 3 + y);
    lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
    const auto selector = LV_PART_MAIN | LV_STATE_DEFAULT;
    lv_obj_set_style_text_font(
        row, launcher_fonts().get("AlibabaPuHuiTi-3-55-Regular.ttf", 16, LV_FREETYPE_FONT_STYLE_NORMAL), selector);
    lv_obj_set_style_pad_left(row, 8, selector);
    lv_obj_set_style_pad_right(row, 8, selector);
    lv_obj_set_style_pad_top(row, 2, selector);
    lv_obj_set_style_radius(row, 6, selector);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x444444), selector);
    return row;
}

void style_list_row(lv_obj_t *row, bool selected)
{
    const auto selector = LV_PART_MAIN | LV_STATE_DEFAULT;
    lv_obj_set_style_bg_opa(row, selected ? LV_OPA_COVER : LV_OPA_TRANSP, selector);
    lv_obj_set_style_text_color(row, lv_color_hex(selected ? 0xFFFFFF : 0x888888), selector);
}

// The app's Settings > Launcher entry, if it has one: the same filter that page uses to list it.
bool hideable_descriptor(const app &item, AppDescriptor &out)
{
    std::size_t count = 0;
    const AppDescriptor *entries = launcher_app_registry_entries(&count);
    for (std::size_t i = 0; entries && i < count; ++i) {
        const AppDescriptor &d = entries[i];
        if (d.configurable && d.label && item.Name == d.label && d.config_key && d.config_key[0] &&
            settings_t12b::launcher::is_launcher_settings_origin(d.origin) &&
            !settings_t12b::launcher::is_protected_launcher_app(d.config_key)) {
            out = d;
            return true;
        }
    }
    return false;
}

// Built-in pages have no Exec, so they are keyed by name.
std::string favorite_key(const app &item)
{
    return item.Exec.empty() ? "name:" + item.Name : item.Exec;
}

std::string read_favorites()
{
    std::string stored;
    try {
        cp0_signal_config_api({"GetStr", kHomeFavoritesConfigKey, ""},
                              [&](int code, std::string data) { if (code == 0) stored = std::move(data); });
    } catch (...) {
    }
    return stored;
}

bool config_call(std::initializer_list<std::string> args)
{
    bool succeeded = false;
    try {
        cp0_signal_config_api(std::list<std::string>(args),
                              [&](int code, std::string) { succeeded = code == 0; });
    } catch (...) {
    }
    return succeeded;
}

// Same rollback as the Settings list-view toggle: SetStr is live at once, so undo it if Save fails.
bool write_favorites(const std::string &previous, const std::string &desired)
{
    if (!config_call({"SetStr", kHomeFavoritesConfigKey, desired})) return false;
    if (config_call({"Save"})) return true;
    if (config_call({"SetStr", kHomeFavoritesConfigKey, previous})) (void)config_call({"Save"});
    return false;
}

std::string read_file(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// .desktop path -> latest dpkg "install" of its package (upgrades do not count; file times do,
// since dpkg keeps the build time and replaces the file on upgrade). Rebuilt when dpkg.log changes.
// shortcut: reads dpkg.log and dpkg.log.1 only; installs rotated into .gz count as oldest.
const std::map<std::string, long long> &desktop_install_times(const std::string &app_dir)
{
    static std::map<std::string, long long> times;
    static std::filesystem::file_time_type seen{};
    static std::uintmax_t seen_size = 0;
    std::error_code error;
    const std::string log = "/var/log/dpkg.log";
    const auto modified = std::filesystem::last_write_time(log, error);
    const std::uintmax_t size = error ? 0 : std::filesystem::file_size(log, error);
    if (error || (modified == seen && size == seen_size)) return times;
    seen = modified;
    seen_size = size;

    std::map<std::string, long long> installs;
    launcher_dpkg_log_installs(read_file(log + ".1"), installs);
    launcher_dpkg_log_installs(read_file(log), installs);
    times.clear();
    for (const auto &[package, stamp] : installs) {
        // Multi-arch packages keep pkg:arch.list, the others pkg.list.
        std::string list = read_file("/var/lib/dpkg/info/" + package + ".list");
        if (list.empty()) list = read_file("/var/lib/dpkg/info/" + package.substr(0, package.find(':')) + ".list");
        for (const std::string &desktop : launcher_dpkg_list_desktops(list, app_dir)) times[desktop] = stamp;
    }
    return times;
}

} // namespace

void UILaunchPage::create_list_view(lv_obj_t *parent)
{
    if (!parent) return;
    lv_obj_t *container = lv_obj_create(parent);
    if (!container) return;
    lv_obj_set_size(container, 320, 150);
    lv_obj_set_align(container, LV_ALIGN_CENTER);
    lv_obj_clear_flag(container, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);

    std::array<lv_obj_t *, kListRows> rows = {};
    bool complete = true;
    for (size_t i = 0; i < kListRows; ++i) {
        rows[i] = create_list_row(container, static_cast<lv_coord_t>(i) * kRowHeight);
        complete = complete && rows[i];
    }

    std::array<lv_obj_t *, kListCards> cards = {};
    for (lv_obj_t *&card : cards) {
        card = lv_obj_create(container);
        if (!card) {
            lv_obj_delete(container);
            return;
        }
        // Same look as the carousel's cards, so set_panel_icon sizes the icon the same way.
        const auto selector = LV_PART_MAIN | LV_STATE_DEFAULT;
        lv_obj_set_align(card, LV_ALIGN_CENTER);
        lv_obj_clear_flag(card, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, selector);
        // Invisible border: keeps the carousel's icon size (and pool entries) without the frame.
        lv_obj_set_style_border_opa(card, LV_OPA_TRANSP, selector);
    }
    lv_obj_t *scroll = lv_obj_create(container);
    if (!complete || !scroll) {
        lv_obj_delete(container);
        return;
    }
    const auto selector = LV_PART_MAIN | LV_STATE_DEFAULT;
    lv_obj_clear_flag(scroll, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_border_width(scroll, 0, selector);
    lv_obj_set_style_radius(scroll, 1, selector);
    lv_obj_set_style_bg_color(scroll, lv_color_hex(0x666666), selector);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_COVER, selector);

    list_container_ = container;
    list_rows_ = rows;
    list_cards_ = cards;
    list_scroll_ = scroll;
    lv_obj_add_event_cb(container, on_owned_obj_deleted, LV_EVENT_DELETE, this);
}

void UILaunchPage::forget_list_view()
{
    list_menu_ = nullptr;  // a child of list_container_, gone with it
    list_menu_rows_.clear();
    list_menu_items_.clear();
    list_tab_armed_ = false;
    stop_list_enter_hold();
    list_enter_swallow_ = false;
    list_container_ = nullptr;
    list_rows_.fill(nullptr);
    list_cards_.fill(nullptr);
    list_scroll_ = nullptr;
    lv_anim_delete(this, nullptr);
}

void UILaunchPage::delete_list_view()
{
    if (!list_container_) return;
    lv_obj_remove_event_cb_with_user_data(list_container_, UILaunchPage::on_owned_obj_deleted, this);
    lv_obj_delete(list_container_);
    forget_list_view();
}

// Show the container that matches the setting. An in-flight carousel switch
// is left to finish on the hidden carousel (its selection is already committed).
void UILaunchPage::apply_home_view()
{
    close_list_menu();
    list_mode_ = launcher_home_list_view_enabled() && list_container_;
    if (carousel_container_) {
        if (list_mode_) lv_obj_add_flag(carousel_container_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(carousel_container_, LV_OBJ_FLAG_HIDDEN);
    }
    if (list_container_) {
        if (list_mode_) lv_obj_clear_flag(list_container_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(list_container_, LV_OBJ_FLAG_HIDDEN);
    }
    if (list_mode_) refresh_list_view();
    else if (launch_ && carousel_container_) refresh_carousel();  // selection may have moved in list view
}

void UILaunchPage::rebuild_list_order()
{
    list_names_.clear();
    list_favorite_.clear();
    std::vector<long long> installed;
    int sort = 0;
    try {
        cp0_signal_config_api({"GetInt", kHomeListSortConfigKey, "0"},
                              [&](int code, std::string data) { if (code == 0 && data == "1") sort = 1; });
    } catch (...) {
    }
    list_sort_recent_ = sort == 1;
    const std::vector<std::string> favorites = launcher_favorites_parse(read_favorites());
    const std::map<std::string, long long> *times =
        list_sort_recent_ ? &desktop_install_times(launcher_platform::path("applications")) : nullptr;
    const size_t count = launch_ ? launch_->app_count() : 0;
    for (size_t i = 0; i < count; ++i) {
        const app *item = launch_->app_at(i);
        list_names_.push_back(item ? item->Name : std::string());
        list_favorite_.push_back(item && std::find(favorites.begin(), favorites.end(), favorite_key(*item)) != favorites.end());
        if (times) {
            auto found = item ? times->find(item->DesktopPath) : times->end();
            installed.push_back(found == times->end() ? LLONG_MIN : found->second);
        }
    }
    list_order_ = launcher_list_order(list_names_, list_favorite_, installed);
    const size_t selected = launch_ ? launch_->current_app_index() : 0;
    auto found = std::find(list_order_.begin(), list_order_.end(), selected);
    list_pos_ = found == list_order_.end() ? 0 : static_cast<size_t>(found - list_order_.begin());
}

void UILaunchPage::refresh_list_view()
{
    if (!list_container_ || !list_mode_) return;
    close_list_menu();  // the app list may have changed under it
    // Launch keeps current_app as a product-order index, which points at another app once one is
    // removed; keep the same app by name, or the one that took its list row.
    const size_t old_pos = list_pos_;
    const std::string old_name = list_pos_ < list_order_.size() && list_order_[list_pos_] < list_names_.size()
                                     ? list_names_[list_order_[list_pos_]]
                                     : std::string();
    rebuild_list_order();
    if (!old_name.empty() && !list_order_.empty()) {
        size_t pos = std::min(old_pos, list_order_.size() - 1);
        for (size_t i = 0; i < list_order_.size(); ++i)
            if (list_names_[list_order_[i]] == old_name) pos = i;
        if (pos != list_pos_ && launch_) {
            list_pos_ = pos;
            launch_->select_app(list_order_[pos]);
        }
    }
    update_list_view();
}

void UILaunchPage::update_list_view()
{
    stop_list_anim();
    const size_t count = list_order_.size();
    if (list_pos_ >= count) list_pos_ = 0;
    if (list_pos_ < list_top_) list_top_ = list_pos_;
    if (list_pos_ >= list_top_ + kListRows) list_top_ = list_pos_ + 1 - kListRows;
    if (count <= kListRows || list_top_ + kListRows > count) list_top_ = count > kListRows ? count - kListRows : 0;

    for (size_t i = 0; i < kListRows; ++i) {
        lv_obj_t *row = list_rows_[i];
        if (!row) continue;
        const size_t position = list_top_ + i;
        if (position >= count) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
        const size_t index = list_order_[position];
        // U+2605 exists in the row font (checked in the TTF); the LV_SYMBOL_* glyphs would not.
        lv_label_set_text(row, ((list_favorite_[index] ? "\u2605 " : "") + list_names_[index]).c_str());
        style_list_row(row, position == list_pos_);
    }

    // Thin thumb at the right edge showing the visible window; hidden when everything fits.
    if (list_scroll_) {
        if (count <= kListRows) {
            lv_obj_add_flag(list_scroll_, LV_OBJ_FLAG_HIDDEN);
        } else {
            constexpr lv_coord_t kTrackTop = 4, kTrackHeight = 142;
            const lv_coord_t thumb = std::max<lv_coord_t>(10, kTrackHeight * kListRows / count);
            const lv_coord_t y = kTrackTop + (kTrackHeight - thumb) * list_top_ / (count - kListRows);
            lv_obj_set_size(list_scroll_, 2, thumb);
            lv_obj_set_pos(list_scroll_, 317, y);
            lv_obj_clear_flag(list_scroll_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    place_list_cards(0);
    for (size_t i = 0; i < kListCards; ++i) {
        lv_obj_t *card = list_cards_[i];
        if (!card) continue;
        // Side cards need a neighbour: with one app only the centre card shows.
        if (count < 2 && i != kListCards / 2) {
            lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
        const app *item = nullptr;
        if (count) item = launch_->app_at(list_order_[(list_pos_ + 2 * count + i - kListCards / 2) % count]);
        set_panel_icon(card, item ? item->Icon : std::string());
    }
}

// Card i sits at slot i-2 (kCardPitch apart); offset slides all of them, sizes follow the distance from the centre.
void UILaunchPage::place_list_cards(int offset)
{
    const auto selector = LV_PART_MAIN | LV_STATE_DEFAULT;
    for (size_t i = 0; i < kListCards; ++i) {
        lv_obj_t *card = list_cards_[i];
        if (!card) continue;
        const int y = (static_cast<int>(i) - static_cast<int>(kListCards / 2)) * kCardPitch + offset;
        const int far = std::min(std::abs(y), static_cast<int>(kCardPitch));
        const lv_coord_t size = kCenterCard - (kCenterCard - kSideCard) * far / kCardPitch;
        lv_obj_set_size(card, size, size);
        lv_obj_set_pos(card, kCardX, y);
        lv_obj_set_style_radius(card, 22 - 5 * far / kCardPitch, selector);
    }
}

void UILaunchPage::stop_list_anim()
{
    lv_anim_delete(this, nullptr);
}

// Slide from the previous layout (shifted by dir cards) to the rest layout; update_list_view already set the content.
void UILaunchPage::start_list_anim(int dir)
{
    place_list_cards(dir * kCardPitch);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);
    lv_anim_set_values(&anim, dir * kCardPitch, 0);
    lv_anim_set_duration(&anim, kListAnimMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, [](void *var, int32_t value) {
        static_cast<UILaunchPage *>(var)->place_list_cards(value);
    });
    // The slide leaves the icon scale of the old card size; redo it at the rest sizes.
    lv_anim_set_completed_cb(&anim, [](lv_anim_t *a) {
        static_cast<UILaunchPage *>(a->var)->update_list_view();
    });
    lv_anim_start(&anim);
}

bool UILaunchPage::list_move(int delta, bool wrap)
{
    if (!launch_ || list_order_.empty()) return false;
    const size_t next = launcher_list_step(list_order_.size(), list_pos_, delta, wrap);
    if (next == list_pos_) return false;
    list_pos_ = next;
    launch_->select_app(list_order_[list_pos_]);
    update_list_view();  // snaps any running slide to the new rest layout first
    // Only a single step slides; with 2 apps the direction is ambiguous, so it snaps too.
    if ((delta == 1 || delta == -1) && list_order_.size() > 2) start_list_anim(delta);
    return true;
}

bool UILaunchPage::list_jump(char letter)
{
    if (!launch_ || list_order_.empty()) return false;
    const int next = launcher_list_jump(list_names_, list_order_, list_pos_, letter, favorite_count());
    if (next < 0 || static_cast<size_t>(next) == list_pos_) return false;
    list_pos_ = static_cast<size_t>(next);
    launch_->select_app(list_order_[list_pos_]);
    update_list_view();
    return true;
}

size_t UILaunchPage::favorite_count() const
{
    return static_cast<size_t>(std::count(list_favorite_.begin(), list_favorite_.end(), 1));
}

// Space: next favorite, or the first one from elsewhere. Single steps slide like list_move.
bool UILaunchPage::list_cycle_favorite()
{
    if (!launch_ || list_order_.empty()) return false;
    const int next = launcher_favorite_next(list_pos_, favorite_count());
    if (next < 0 || static_cast<size_t>(next) == list_pos_) return false;
    const int delta = next - static_cast<int>(list_pos_);
    if (delta == 1 || delta == -1) return list_move(delta, false);
    list_pos_ = static_cast<size_t>(next);
    launch_->select_app(list_order_[list_pos_]);
    update_list_view();
    return true;
}

// Held-Enter menu. Items are rebuilt per open and per step (actions -> delete confirmation -> deleting).
void UILaunchPage::open_list_menu()
{
    if (list_menu_ || !launch_ || !list_container_ || list_order_.empty()) return;
    if (delete_running_) {  // the job outlives a hidden menu; only show its state again
        show_list_menu("Deleting " + delete_name_ + "...", {}, 0);
        return;
    }
    const app *item = launch_->app_at(list_order_[list_pos_]);
    if (!item) return;
    list_menu_exec_ = favorite_key(*item);
    std::vector<ListMenuAction> items = {ListMenuAction::Favorite};
    AppDescriptor descriptor{};
    if (!item->StoreDesktopPath.empty())
        items.push_back(ListMenuAction::Delete);
    else if (hideable_descriptor(*item, descriptor))
        items.push_back(ListMenuAction::Hide);
    items.push_back(ListMenuAction::Cancel);
    show_list_menu("", std::move(items), 0);
}

// Short Tab: how the non-favorites are ordered. The current order is preselected.
void UILaunchPage::open_sort_menu()
{
    if (list_menu_ || !list_container_) return;
    show_list_menu("Sort apps", {ListMenuAction::SortAlphabetical, ListMenuAction::SortRecent},
                   list_sort_recent_ ? 1 : 0);
}

void UILaunchPage::set_list_sort(bool recent)
{
    if (recent == list_sort_recent_) return;
    const char *previous = list_sort_recent_ ? "1" : "0";
    if (!config_call({"SetInt", kHomeListSortConfigKey, recent ? "1" : "0"})) {
        launcher_toast().show("Unable to save sorting");
        return;
    }
    if (!config_call({"Save"})) {  // SetInt is live at once: undo it
        if (config_call({"SetInt", kHomeListSortConfigKey, previous})) (void)config_call({"Save"});
        launcher_toast().show("Unable to save sorting");
        return;
    }
    refresh_list_view();  // keeps the selected app selected
}

// Replaces the menu box; list_menu_exec_ survives. A non-empty title adds an unselectable first row.
void UILaunchPage::show_list_menu(const std::string &title, std::vector<ListMenuAction> items, size_t selected)
{
    if (!list_container_) return;
    const std::string exec = list_menu_exec_;
    close_list_menu();
    list_menu_exec_ = exec;
    list_menu_items_ = std::move(items);
    list_menu_sel_ = selected;
    const lv_coord_t top = title.empty() ? 0 : kRowHeight;

    const auto selector = LV_PART_MAIN | LV_STATE_DEFAULT;
    lv_obj_t *box = lv_obj_create(list_container_);
    if (!box) {
        close_list_menu();
        return;
    }
    lv_obj_set_size(box, 200, top + static_cast<lv_coord_t>(list_menu_items_.size()) * kRowHeight + 6);
    lv_obj_set_align(box, LV_ALIGN_CENTER);
    lv_obj_clear_flag(box, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_pad_all(box, 0, selector);
    lv_obj_set_style_radius(box, 8, selector);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x222222), selector);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, selector);
    lv_obj_set_style_border_width(box, 1, selector);
    lv_obj_set_style_border_color(box, lv_color_hex(0x666666), selector);

    bool complete = true;
    if (!title.empty()) {
        lv_obj_t *label = create_list_row(box, 0);
        if (label) {
            lv_label_set_text(label, title.c_str());
            style_list_row(label, false);
            lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), selector);
        } else {
            complete = false;
        }
    }
    std::vector<lv_obj_t *> rows;
    for (size_t i = 0; complete && i < list_menu_items_.size(); ++i) {
        lv_obj_t *row = create_list_row(box, top + static_cast<lv_coord_t>(i) * kRowHeight);
        if (!row) {
            complete = false;
            break;
        }
        rows.push_back(row);
    }
    if (!complete) {
        lv_obj_delete(box);
        close_list_menu();
        return;
    }
    list_menu_ = box;
    list_menu_rows_ = std::move(rows);
    update_list_menu();
}

void UILaunchPage::close_list_menu()
{
    if (list_menu_) lv_obj_delete(list_menu_);
    list_menu_ = nullptr;
    list_menu_rows_.clear();
    list_menu_items_.clear();
    list_menu_exec_.clear();
}

void UILaunchPage::update_list_menu()
{
    if (!list_menu_) return;
    const bool favorite = list_pos_ < list_order_.size() && list_favorite_[list_order_[list_pos_]];
    for (size_t i = 0; i < list_menu_rows_.size(); ++i) {
        const char *text = "Cancel";
        switch (list_menu_items_[i]) {
        case ListMenuAction::Favorite: text = favorite ? "Remove from favorites" : "Add to favorites"; break;
        case ListMenuAction::Hide: text = "Hide"; break;
        case ListMenuAction::Delete:
        case ListMenuAction::ConfirmDelete: text = "Delete"; break;
        case ListMenuAction::Cancel: break;
        case ListMenuAction::SortAlphabetical: text = "A-Z"; break;
        case ListMenuAction::SortRecent: text = "Recently installed"; break;
        }
        lv_label_set_text(list_menu_rows_[i], text);
        style_list_row(list_menu_rows_[i], i == list_menu_sel_);
    }
}

void UILaunchPage::activate_list_menu(ListMenuAction action)
{
    if (action == ListMenuAction::Delete) {
        const app *item = selected_store_app();
        if (!item) {
            close_list_menu();
            return;
        }
        show_list_menu("Delete " + item->Name + "?", {ListMenuAction::ConfirmDelete, ListMenuAction::Cancel}, 1);
        return;
    }
    if (action == ListMenuAction::ConfirmDelete) {
        begin_list_delete();
        return;
    }
    close_list_menu();
    if (action == ListMenuAction::Favorite) toggle_selected_favorite();
    if (action == ListMenuAction::Hide) hide_selected_app();
    if (action == ListMenuAction::SortAlphabetical || action == ListMenuAction::SortRecent)
        set_list_sort(action == ListMenuAction::SortRecent);
}

// Uninstall through the App Store backend so its records stay right (never run for stock apps):
// user-side prepare on a worker thread, the root helper via cp0_sudo, user-side finalize on a worker.
// Every hop back to the UI goes through lv_async_call; delete_alive_ guards a page that is gone by then.
struct UILaunchPage::DeleteJob {
    UILaunchPage *page = nullptr;
    std::weak_ptr<bool> alive;
    std::string name, exec, desktop_path, backend, installed_path, app_id, output, error;
    LauncherUninstallJob job;

    static int capture(const std::vector<std::string> &args, std::string &out)
    {
        std::vector<const char *> argv;
        for (const std::string &arg : args) argv.push_back(arg.c_str());
        argv.push_back(nullptr);
        char buffer[8192];
        const int rc = cp0_process_capture_argv(argv.data(), buffer, sizeof(buffer));
        out = buffer;
        return rc;
    }

    static void post(const std::shared_ptr<DeleteJob> &self, lv_async_cb_t callback)
    {
        auto *holder = new (std::nothrow) std::shared_ptr<DeleteJob>(self);
        if (holder && lv_async_call(callback, holder) != LV_RESULT_OK) delete holder;
        // shortcut: a failed post leaves the job unreported, only possible when out of memory
    }

    static std::shared_ptr<DeleteJob> take(void *user)
    {
        std::unique_ptr<std::shared_ptr<DeleteJob>> holder(static_cast<std::shared_ptr<DeleteJob> *>(user));
        return holder ? *holder : nullptr;
    }

    static void finish(const std::shared_ptr<DeleteJob> &self)
    {
        const bool ok = self->error.empty();
        const std::string message = ok ? "Deleted " + self->name : self->error;
        const auto alive = self->alive.lock();
        if (alive && *alive) self->page->finish_list_delete(self->exec, ok, message);
        else launcher_toast().show(message.c_str());
    }

    static void prepare(const std::shared_ptr<DeleteJob> &self)  // worker thread
    {
        std::ifstream file(self->installed_path);
        std::stringstream contents;
        contents << file.rdbuf();
        self->app_id = launcher_uninstall_find_app_id(contents.str(), self->desktop_path);
        if (self->app_id.empty()) {
            self->error = "Not installed by the App Store; remove it with apt";
        } else {
            std::string out;
            capture({self->backend, "--prepare-package", "uninstall", self->app_id}, out);
            if (!launcher_uninstall_parse_job(out, self->job)) {
                self->error = launcher_uninstall_first_error(out);
                if (self->error.empty()) self->error = "Unable to prepare the uninstall";
            }
        }
        post(self, after_prepare);
    }

    static void after_prepare(void *user) noexcept
    {
        try {
            auto self = take(user);
            if (!self) return;
            if (self->error.empty()) start_sudo(self);
            if (!self->error.empty()) finish(self);
        } catch (...) {
        }
    }

    static void start_sudo(const std::shared_ptr<DeleteJob> &self)
    {
        const std::vector<std::string> args = launcher_uninstall_helper_argv(self->backend, self->job);
        std::vector<const char *> argv;
        for (const std::string &arg : args) argv.push_back(arg.c_str());
        argv.push_back(nullptr);
        auto *holder = new std::shared_ptr<DeleteJob>(self);
        // Same timeouts as the App Store's sudo runner: 60 s to authenticate, 2 h + margin to run.
        const int rc = cp0_sudo_run_argv_async_ex(argv.data(), CP0_SUDO_CALLBACK_LVGL, sudo_output,
                                                  sudo_complete, holder, 60 * 1000, 7500000, nullptr);
        if (rc != 0) {
            delete holder;
            self->error = "Unable to start sudo request";
        }
    }

    static void sudo_output(const char *data, size_t size, void *user)
    {
        auto *self = static_cast<std::shared_ptr<DeleteJob> *>(user);
        if (data && self && (*self)->output.size() < 65536) (*self)->output.append(data, size);
    }

    static void sudo_complete(cp0_sudo_result_t result, int exit_code, void *user)
    {
        try {
            auto self = take(user);
            if (!self) return;
            if (result == CP0_SUDO_RESULT_SUCCESS) {
                try {
                    std::thread([self] { finalize(self); }).detach();
                    return;
                } catch (...) {
                    self->error = "Unable to start the finalize step";
                }
            } else if (result == CP0_SUDO_RESULT_AUTH_FAILED) {
                self->error = "Sudo authentication failed";
            } else if (result == CP0_SUDO_RESULT_CANCELLED) {
                self->error = "Delete cancelled";
            } else if (result == CP0_SUDO_RESULT_TIMED_OUT) {
                self->error = "Delete timed out";
            } else {
                self->error = launcher_uninstall_first_error(self->output);
                if (self->error.empty()) self->error = "Delete failed (" + std::to_string(exit_code) + ")";
            }
            finish(self);
        } catch (...) {
        }
    }

    static void finalize(const std::shared_ptr<DeleteJob> &self)  // worker thread
    {
        std::string out;
        const int rc = capture({self->backend, "--finalize-package", "uninstall", self->app_id,
                                self->job.transaction_id}, out);
        self->error = launcher_uninstall_first_error(out);
        if (self->error.empty() && rc != 0) self->error = "Unable to finalize the uninstall";
        post(self, after_finalize);
    }

    static void after_finalize(void *user) noexcept
    {
        try {
            if (auto self = take(user)) finish(self);
        } catch (...) {
        }
    }
};

// The menu captures keys, so the selection is still the app it was opened for.
const app *UILaunchPage::selected_store_app() const
{
    if (!launch_ || list_pos_ >= list_order_.size()) return nullptr;
    const app *item = launch_->app_at(list_order_[list_pos_]);
    return item && !item->StoreDesktopPath.empty() ? item : nullptr;
}

void UILaunchPage::begin_list_delete()
{
    if (delete_running_ || !launch_) return;
    // Re-check the origin here: stock apps are never deleted, whatever menu state got us here.
    const app *item = selected_store_app();
    if (!item) {
        close_list_menu();
        return;
    }
    auto job = std::make_shared<DeleteJob>();
    job->page = this;
    job->alive = delete_alive_;
    job->name = item->Name;
    job->exec = favorite_key(*item);
    job->desktop_path = item->StoreDesktopPath;
    job->backend = launcher_platform::path("appstore_exec");
    job->installed_path = launcher_platform::path("home_dir") + "/.local/share/cardputerzero-appstore/installed.json";
    if (job->backend.empty()) {
        close_list_menu();
        launcher_toast().show("App Store not found");
        return;
    }
    delete_running_ = true;
    delete_name_ = job->name;
    show_list_menu("Deleting " + delete_name_ + "...", {}, 0);
    try {
        std::thread([job] { DeleteJob::prepare(job); }).detach();
    } catch (...) {
        job->error = "Unable to start the delete";
        DeleteJob::finish(job);
    }
}

// UI thread, after the job ended. The app list itself reloads through the applications directory watcher.
void UILaunchPage::finish_list_delete(const std::string &exec, bool ok, const std::string &message)
{
    delete_running_ = false;
    delete_name_.clear();
    close_list_menu();  // only the "Deleting" box can be open while a job runs
    if (ok) {
        const std::string previous = read_favorites();
        std::vector<std::string> favorites = launcher_favorites_parse(previous);
        if (std::find(favorites.begin(), favorites.end(), exec) != favorites.end())
            (void)write_favorites(previous, launcher_favorites_join(launcher_favorites_toggle(favorites, exec)));
    }
    launcher_toast().show(message.c_str());
}

// What Settings > Launcher's toggle does: disable the app in the registry, then reload the app list.
void UILaunchPage::hide_selected_app()
{
    if (!launch_ || list_pos_ >= list_order_.size()) return;
    const app *item = launch_->app_at(list_order_[list_pos_]);
    AppDescriptor descriptor{};
    if (!item || !hideable_descriptor(*item, descriptor)) return;
    const std::string name = item->Name;  // the reload below replaces the app list
    bool hidden = false;
    try {
        hidden = launcher_app_registry_set_enabled(descriptor, false);
    } catch (...) {
    }
    if (!hidden) {
        launcher_toast().show("Unable to hide the app");
        return;
    }
    launcher_app_registry_notify_changed();
    launcher_toast().show((name + " hidden (Settings > Launcher)").c_str());
}

// Flip the favorite flag of the selected app; the order is rebuilt and the same app stays selected.
bool UILaunchPage::toggle_selected_favorite()
{
    if (!launch_ || list_pos_ >= list_order_.size()) return false;
    const app *item = launch_->app_at(list_order_[list_pos_]);
    if (!item) return false;
    const std::string previous = read_favorites();
    const std::string desired = launcher_favorites_join(launcher_favorites_toggle(launcher_favorites_parse(previous), favorite_key(*item)));
    if (!write_favorites(previous, desired)) {
        launcher_toast().show("Unable to save favorites");
        return false;
    }
    rebuild_list_order();
    update_list_view();
    return true;
}
