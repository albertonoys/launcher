/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Indices of names sorted A-Z, case-insensitive; equal names keep their input order.
std::vector<size_t> launcher_alphabetical_order(const std::vector<std::string> &names);

// Like launcher_alphabetical_order, but favorites (favorite[i] != 0) come first; both groups A-Z.
// A shorter `favorite` counts as "not a favorite" for the missing entries.
// With `installed` (install times, per name), the non-favorites go newest first instead,
// equal times A-Z; a shorter `installed` counts as the oldest (any value, negatives included, is newer).
std::vector<size_t> launcher_list_order(const std::vector<std::string> &names,
                                        const std::vector<char> &favorite,
                                        const std::vector<long long> &installed = {});

// Stored favorites (config `home_favorites`) are Exec strings joined by '\n'.
std::vector<std::string> launcher_favorites_parse(const std::string &stored);
std::string launcher_favorites_join(const std::vector<std::string> &execs);
// `execs` with `exec` removed if present, appended otherwise. Empty `exec` changes nothing.
std::vector<std::string> launcher_favorites_toggle(std::vector<std::string> execs, const std::string &exec);

// Position after moving delta rows in a list of count rows (wrap around or clamp).
size_t launcher_list_step(size_t count, size_t position, int delta, bool wrap);

// Next position after `position` (wrapping, `position` itself last) whose name
// starts with `letter`, case-insensitive; -1 if none. Only positions >= `first` are
// searched (the list view passes the favorites count so jumps skip the favorites block).
int launcher_list_jump(const std::vector<std::string> &names, const std::vector<size_t> &order,
                       size_t position, char letter, size_t first = 0);

// Position of the next favorite (the first `favorites` positions): the one after `position`,
// or the first when `position` is the last favorite or not a favorite; -1 without favorites.
int launcher_favorite_next(size_t position, size_t favorites);

// Launcher config int: 0 = carousel home screen (default), 1 = list view.
// Defined in settings/settings_adapter.cpp, next to the Settings toggle.
constexpr const char *kHomeFavoritesConfigKey = "home_favorites";
constexpr const char *kHomeListViewConfigKey = "home_list_view";
// Launcher config int: 0 = non-favorites A-Z (default), 1 = most recently installed first.
constexpr const char *kHomeListSortConfigKey = "home_list_sort";
bool launcher_home_list_view_enabled();
