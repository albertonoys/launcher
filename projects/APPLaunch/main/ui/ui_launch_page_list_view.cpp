/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_launch_page.h"

#include "launch.h"
#include "hal_lvgl_bsp.h"
#include "launcher_toast.h"
#include "model/launcher_list_model.hpp"

#include <algorithm>
#include <cstdlib>
#include <list>

namespace {

constexpr lv_coord_t kRowHeight = 24;
constexpr lv_coord_t kRowsWidth = 190;
constexpr lv_coord_t kCenterCard = 100;   // same sizes as the carousel, so the icon pool fits
constexpr lv_coord_t kSideCard = 80;
constexpr lv_coord_t kCardPitch = 99;     // centre-to-centre, as in the horizontal carousel
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
    const std::vector<std::string> favorites = launcher_favorites_parse(read_favorites());
    const size_t count = launch_ ? launch_->app_count() : 0;
    for (size_t i = 0; i < count; ++i) {
        const app *item = launch_->app_at(i);
        list_names_.push_back(item ? item->Name : std::string());
        list_favorite_.push_back(item && std::find(favorites.begin(), favorites.end(), item->Exec) != favorites.end());
    }
    list_order_ = launcher_list_order(list_names_, list_favorite_);
    const size_t selected = launch_ ? launch_->current_app_index() : 0;
    auto found = std::find(list_order_.begin(), list_order_.end(), selected);
    list_pos_ = found == list_order_.end() ? 0 : static_cast<size_t>(found - list_order_.begin());
}

void UILaunchPage::refresh_list_view()
{
    if (!list_container_ || !list_mode_) return;
    close_list_menu();  // the app list may have changed under it
    rebuild_list_order();
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

// Card i sits at slot i-2 (pitch 99); offset slides all of them, sizes follow the distance from the centre.
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

// Tab menu. Items are rebuilt per open; Phase B pushes a Delete item (and handles it in activate_list_menu).
void UILaunchPage::open_list_menu()
{
    if (list_menu_ || !launch_ || !list_container_ || list_order_.empty()) return;
    const app *item = launch_->app_at(list_order_[list_pos_]);
    if (!item) return;
    list_menu_exec_ = item->Exec;
    list_menu_items_ = {ListMenuAction::Favorite, ListMenuAction::Cancel};
    list_menu_sel_ = 0;

    const auto selector = LV_PART_MAIN | LV_STATE_DEFAULT;
    lv_obj_t *box = lv_obj_create(list_container_);
    if (!box) return;
    lv_obj_set_size(box, 200, static_cast<lv_coord_t>(list_menu_items_.size()) * kRowHeight + 6);
    lv_obj_set_align(box, LV_ALIGN_CENTER);
    lv_obj_clear_flag(box, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_pad_all(box, 0, selector);
    lv_obj_set_style_radius(box, 8, selector);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x222222), selector);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, selector);
    lv_obj_set_style_border_width(box, 1, selector);
    lv_obj_set_style_border_color(box, lv_color_hex(0x666666), selector);

    std::vector<lv_obj_t *> rows;
    for (size_t i = 0; i < list_menu_items_.size(); ++i) {
        lv_obj_t *row = create_list_row(box, static_cast<lv_coord_t>(i) * kRowHeight);
        if (!row) {
            lv_obj_delete(box);
            return;
        }
        rows.push_back(row);
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
        const bool is_favorite_item = list_menu_items_[i] == ListMenuAction::Favorite;
        lv_label_set_text(list_menu_rows_[i], is_favorite_item ? (favorite ? "Remove from favorites" : "Add to favorites")
                                                               : "Cancel");
        style_list_row(list_menu_rows_[i], i == list_menu_sel_);
    }
}

void UILaunchPage::activate_list_menu(ListMenuAction action)
{
    close_list_menu();
    if (action == ListMenuAction::Favorite) toggle_selected_favorite();
}

// Flip the favorite flag of the selected app; the order is rebuilt and the same app stays selected.
bool UILaunchPage::toggle_selected_favorite()
{
    if (!launch_ || list_pos_ >= list_order_.size()) return false;
    const app *item = launch_->app_at(list_order_[list_pos_]);
    if (!item) return false;
    const std::string previous = read_favorites();
    const std::string desired = launcher_favorites_join(launcher_favorites_toggle(launcher_favorites_parse(previous), item->Exec));
    if (!write_favorites(previous, desired)) {
        launcher_toast().show("Unable to save favorites");
        return false;
    }
    rebuild_list_order();
    update_list_view();
    return true;
}
