/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "home_icon_buffer_pool.hpp"
#include "launcher_ui_app_page.hpp"
#include "model/launcher_navigation_model.hpp"
#include "model/launcher_startup_contract.hpp"
#include <atomic>
#include <climits>
#include <array>
#include <memory>
#include <string>
#include <vector>

class Launch;
struct key_item;

class UILaunchPage : public home_base
{
public:
    explicit UILaunchPage(Launch *launch);
    ~UILaunchPage();

    void show_home_screen();
    void load_home_screen();
    void start_startup_gif();
    void create_screen();
    enum LauncherCarouselElement : size_t {
        kCardFarLeft = 0,
        kCardLeft,
        kCardCenter,
        kCardRight,
        kCardFarRight,
        kTitleFarLeft,
        kTitleLeft,
        kTitleCenter,
        kTitleRight,
        kTitleFarRight,
        kPageSlider,
        kLauncherCarouselElementCount,
    };

    void init_input_group();
    static void bind_home_input_group();
    static lv_group_t *home_input_group();
    lv_obj_t *panel(size_t slot);
    lv_obj_t *label(size_t slot);

    void refresh_carousel();
    void reload_home_icons(const std::vector<std::string> &icon_paths);
    void update_carousel_slot(size_t slot, const char *title, const std::string &icon);
    void update_carousel_item(lv_obj_t *panel, lv_obj_t *label, const char *title,
                              const std::string &icon);
    void launch_selected_app();
    void refresh_list_view();

protected:
    std::array<lv_obj_t *, kLauncherCarouselElementCount> carousel_elements_ = {};

private:
    struct StartupTimerContext
    {
        UILaunchPage *page = nullptr;
        LauncherStartupGeneration::Token generation = 0;
    };

    struct StartupSoundAsyncState
    {
        std::atomic<bool> alive{true};
        std::atomic<bool> pending{false};
        // Written by the audio worker thread, read by startup_sound_timer_cb on the LVGL thread.
        std::atomic<int> result{kNoResult};
        static constexpr int kNoResult = INT_MIN;
    };

    enum class ListMenuAction { Favorite, Hide, Delete, ConfirmDelete, Cancel, SortAlphabetical, SortRecent };
    struct DeleteJob;
    void create_app_container(lv_obj_t *parent);
    void switch_left();
    void switch_right();
    void fill_left_entering_slot(lv_obj_t *panel, lv_obj_t *label);
    void fill_right_entering_slot(lv_obj_t *panel, lv_obj_t *label);
    void finish_switch_animation();
    void cancel_switch_animation();
    void handle_home_key(lv_event_t *event);
    void handle_list_key(const struct key_item *item, uint32_t code);
    void create_list_view(lv_obj_t *parent);
    void apply_home_view();
    void rebuild_list_order();
    void update_list_view();
    bool list_move(int delta, bool wrap);
    bool list_jump(char letter);
    bool list_cycle_favorite();
    size_t favorite_count() const;
    void open_list_menu();
    void open_sort_menu();
    void set_list_sort(bool recent);
    void show_list_menu(const std::string &title, std::vector<ListMenuAction> items, size_t selected);
    const app *selected_store_app() const;
    void begin_list_delete();
    void finish_list_delete(const std::string &exec, bool ok, const std::string &message);
    void close_list_menu();
    void handle_list_menu_key(const struct key_item *item, uint32_t code);
    void update_list_menu();
    void activate_list_menu(ListMenuAction action);
    bool toggle_selected_favorite();
    void hide_selected_app();
    void stop_list_enter_hold();
    void place_list_cards(int offset);
    void stop_list_anim();
    void start_list_anim(int dir);
    void delete_list_view();
    void forget_list_view();
    void handle_startup_gif_event(lv_event_t *event);
    void play_startup_sound_with_retry();
    void warm_up_audio_mixer();
    void handle_startup_sound_result(int code);
    void stop_startup_presentation();
    void stop_startup_sound_timer();
    void stop_startup_delay();
    void finish_startup_delay();
    void release_startup_screen(lv_obj_t *&screen);

    void rotate_carousel_left(size_t start, size_t end);
    void rotate_carousel_right(size_t start, size_t end);
    void set_page_slider_range(size_t page_count, size_t selected_page);
    void set_page_slider_position(size_t page);
    void set_carousel_element_clickable(size_t element, bool clickable);
    void set_panel_icon(lv_obj_t *panel, const std::string &src);
    static void on_left_arrow_clicked(lv_event_t *event) noexcept;
    static void on_right_arrow_clicked(lv_event_t *event) noexcept;
    static void on_app_clicked(lv_event_t *event) noexcept;
    static void on_home_key(lv_event_t *event) noexcept;
    static void on_startup_gif_event(lv_event_t *event) noexcept;
    static void on_owned_obj_deleted(lv_event_t *event) noexcept;
    static void on_root_deleted(lv_event_t *event) noexcept;
    static void startup_sound_timer_cb(lv_timer_t *timer) noexcept;
    static void startup_delay_timer_cb(lv_timer_t *timer) noexcept;
    static void list_enter_hold_cb(lv_timer_t *timer) noexcept;

    Launch *launch_ = nullptr;
    lv_obj_t *startup_gif_ = nullptr;
    lv_obj_t *left_arrow_button_ = nullptr;
    lv_obj_t *right_arrow_button_ = nullptr;
    lv_obj_t *carousel_container_ = nullptr;
    static constexpr size_t kListRows = 6;
    lv_obj_t *list_container_ = nullptr;
    std::array<lv_obj_t *, kListRows> list_rows_ = {};
    // Vertical carousel: list neighbours -2..+2 of the selection, centre card is index 2.
    static constexpr size_t kListCards = 5;
    std::array<lv_obj_t *, kListCards> list_cards_ = {};
    lv_obj_t *list_scroll_ = nullptr;
    std::vector<std::string> list_names_;
    std::vector<char> list_favorite_;  // per Launch::app_list index
    std::vector<size_t> list_order_;  // list position -> index in Launch::app_list
    bool list_sort_recent_ = false;   // config home_list_sort, read per rebuild
    size_t list_pos_ = 0;
    size_t list_top_ = 0;
    bool list_mode_ = false;
    // Tab menu: a child of list_container_ (deleted with it), built per open.
    lv_obj_t *list_menu_ = nullptr;
    std::vector<lv_obj_t *> list_menu_rows_;
    std::vector<ListMenuAction> list_menu_items_;
    size_t list_menu_sel_ = 0;
    std::string list_menu_exec_;  // favorite key of the app the menu was opened for
    std::shared_ptr<bool> delete_alive_ = std::make_shared<bool>(true);  // false once the page is gone
    bool delete_running_ = false;  // an uninstall job is in flight (it outlives the menu)
    std::string delete_name_;
    bool list_tab_armed_ = false;  // Tab went down with the menu closed and nothing else since
    uint32_t list_tab_down_tick_ = 0;
    lv_timer_t *list_enter_timer_ = nullptr;  // Enter is down: opens the Tab menu if still held
    bool list_enter_swallow_ = false;  // the hold opened the menu; its Enter release must not act
    lv_timer_t *startup_sound_timer_ = nullptr;
    lv_timer_t *startup_delay_timer_ = nullptr;
    StartupTimerContext *startup_sound_context_ = nullptr;
    StartupTimerContext *startup_delay_context_ = nullptr;
    lv_obj_t *startup_logo_scr_ = nullptr;
    std::array<char, 256> startup_gif_path_ = {};
    std::array<char, 256> startup_logo_path_ = {};
    bool startup_gif_done_ = false;
    int startup_sound_retry_count_ = 0;
    LauncherNavigationModel navigation_;
    LauncherStartupDelayModel startup_delay_model_;
    LauncherStartupGeneration startup_sound_generation_;
    LauncherStartupGeneration startup_delay_generation_;
    HomeIconBufferPool home_icon_pool_;
    std::shared_ptr<StartupSoundAsyncState> startup_sound_state_ =
        std::make_shared<StartupSoundAsyncState>();
    std::shared_ptr<bool> carousel_alive_ = std::make_shared<bool>(true);
    bool home_key_registered_ = false;
    bool active_navigation_moves_next_ = false;
};
