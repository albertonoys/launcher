/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

// Settings -> Screen -> Font / Sharp text. Fonts are created once and held by
// every label, so a change is saved and the launcher restarts to apply it.
namespace launcher_ui_font {

constexpr const char *kFamilyConfigKey = "ui_font";      // "" or a family from Cp0FontService::set_ui_font
constexpr const char *kSharpConfigKey = "ui_font_sharp"; // 1: 1-bit text, 0: anti-aliased

std::string family();
bool sharp();
// Hands the saved choice to the font service; call before any UI is built.
void apply();
// Saves the choice and asks the launcher to restart; false when saving failed.
bool set_family(const std::string &family);
bool set_sharp(bool sharp);
bool restart_requested();

} // namespace launcher_ui_font
