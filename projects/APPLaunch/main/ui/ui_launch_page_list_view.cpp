/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ui_launch_page.h"

#include "launch.h"
#include "model/launcher_list_model.hpp"

#include <algorithm>

namespace {

constexpr lv_coord_t kRowHeight = 24;
constexpr lv_coord_t kRowsWidth = 190;
constexpr lv_coord_t kCardSize = 100;

lv_obj_t *create_list_row(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *row = lv_label_create(parent);
    if (!row) return nullptr;
    lv_obj_set_size(row, kRowsWidth, kRowHeight);
    lv_obj_set_pos(row, 4, 3 + y);
    lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
    const auto selector = LV_PART_MAIN | LV_STATE_DEFAULT;
    lv_obj_set_style_text_font(
        row, launcher_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD), selector);
    lv_obj_set_style_pad_left(row, 8, selector);
    lv_obj_set_style_pad_right(row, 8, selector);
    lv_obj_set_style_pad_top(row, 2, selector);
    lv_obj_set_style_radius(row, 6, selector);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x444444), selector);
    return row;
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

    // Same look as the carousel's centre card, so set_panel_icon sizes the icon the same way.
    lv_obj_t *card = lv_obj_create(container);
    lv_obj_t *name = lv_label_create(container);
    if (!complete || !card || !name) {
        lv_obj_delete(container);
        return;
    }
    const auto selector = LV_PART_MAIN | LV_STATE_DEFAULT;
    lv_obj_set_size(card, kCardSize, kCardSize);
    lv_obj_set_pos(card, 210, 8);
    lv_obj_clear_flag(card, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_set_style_radius(card, 12, selector);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, selector);
    lv_obj_set_style_border_color(card, lv_color_hex(0x444444), selector);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, selector);
    lv_obj_set_style_border_post(card, true, selector);

    lv_obj_set_size(name, 120, LV_SIZE_CONTENT);
    lv_obj_set_pos(name, 200, 112);
    lv_obj_set_style_text_font(
        name, launcher_fonts().get("Montserrat-Bold.ttf", 16, LV_FREETYPE_FONT_STYLE_BOLD), selector);
    // Font first: DOT needs a one-line height, which comes from the font.
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, selector);
    lv_obj_set_style_text_color(name, lv_color_hex(0xFFFFFF), selector);

    list_container_ = container;
    list_rows_ = rows;
    list_card_ = card;
    list_name_ = name;
    lv_obj_add_event_cb(container, on_owned_obj_deleted, LV_EVENT_DELETE, this);
}

void UILaunchPage::forget_list_view()
{
    list_container_ = nullptr;
    list_rows_.fill(nullptr);
    list_card_ = nullptr;
    list_name_ = nullptr;
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
    const size_t count = launch_ ? launch_->app_count() : 0;
    for (size_t i = 0; i < count; ++i) {
        const app *item = launch_->app_at(i);
        list_names_.push_back(item ? item->Name : std::string());
    }
    list_order_ = launcher_alphabetical_order(list_names_);
    const size_t selected = launch_ ? launch_->current_app_index() : 0;
    auto found = std::find(list_order_.begin(), list_order_.end(), selected);
    list_pos_ = found == list_order_.end() ? 0 : static_cast<size_t>(found - list_order_.begin());
}

void UILaunchPage::refresh_list_view()
{
    if (!list_container_ || !list_mode_) return;
    rebuild_list_order();
    update_list_view();
}

void UILaunchPage::update_list_view()
{
    const size_t count = list_order_.size();
    if (list_pos_ >= count) list_pos_ = 0;
    if (list_pos_ < list_top_) list_top_ = list_pos_;
    if (list_pos_ >= list_top_ + kListRows) list_top_ = list_pos_ + 1 - kListRows;
    if (count <= kListRows || list_top_ + kListRows > count) list_top_ = count > kListRows ? count - kListRows : 0;

    const auto selector = LV_PART_MAIN | LV_STATE_DEFAULT;
    for (size_t i = 0; i < kListRows; ++i) {
        lv_obj_t *row = list_rows_[i];
        if (!row) continue;
        const size_t position = list_top_ + i;
        if (position >= count) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(row, list_names_[list_order_[position]].c_str());
        const bool selected = position == list_pos_;
        lv_obj_set_style_bg_opa(row, selected ? LV_OPA_COVER : LV_OPA_TRANSP, selector);
        lv_obj_set_style_text_color(row, lv_color_hex(selected ? 0xFFFFFF : 0x888888), selector);
    }

    const app *item = count ? launch_->app_at(list_order_[list_pos_]) : nullptr;
    if (list_name_) lv_label_set_text(list_name_, item ? item->Name.c_str() : "");
    set_panel_icon(list_card_, item ? item->Icon : std::string());
}

bool UILaunchPage::list_move(int delta, bool wrap)
{
    if (!launch_ || list_order_.empty()) return false;
    const size_t next = launcher_list_step(list_order_.size(), list_pos_, delta, wrap);
    if (next == list_pos_) return false;
    list_pos_ = next;
    launch_->select_app(list_order_[list_pos_]);
    update_list_view();
    return true;
}

bool UILaunchPage::list_jump(char letter)
{
    if (!launch_ || list_order_.empty()) return false;
    const int next = launcher_list_jump(list_names_, list_order_, list_pos_, letter);
    if (next < 0 || static_cast<size_t>(next) == list_pos_) return false;
    list_pos_ = static_cast<size_t>(next);
    launch_->select_app(list_order_[list_pos_]);
    update_list_view();
    return true;
}
