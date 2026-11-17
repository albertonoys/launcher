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

// Position after moving delta rows in a list of count rows (wrap around or clamp).
size_t launcher_list_step(size_t count, size_t position, int delta, bool wrap);

// Next position after `position` (wrapping, `position` itself last) whose name
// starts with `letter`, case-insensitive; -1 if none.
int launcher_list_jump(const std::vector<std::string> &names, const std::vector<size_t> &order,
                       size_t position, char letter);
