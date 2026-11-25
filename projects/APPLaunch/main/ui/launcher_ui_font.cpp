/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "launcher_ui_font.hpp"

#include "ui.h"

#include <atomic>

namespace launcher_ui_font {
namespace {

std::atomic<bool> s_restart{false};

bool config(const char *op, const char *key, const std::string &value, std::string *out = nullptr)
{
    bool succeeded = false;
    try {
        cp0_signal_config_api({op, key, value}, [&](int code, std::string data) {
            succeeded = code == 0;
            if (succeeded && out) *out = std::move(data);
        });
    } catch (...) {
    }
    return succeeded;
}

bool save()
{
    bool succeeded = false;
    try {
        cp0_signal_config_api({"Save"}, [&](int code, std::string) { succeeded = code == 0; });
    } catch (...) {
    }
    return succeeded;
}

bool store(const char *op, const char *key, const std::string &value, const std::string &previous)
{
    if (!config(op, key, value)) return false;
    // Set* changes the live config at once; put it back when Save fails.
    if (!save()) {
        (void)config(op, key, previous);
        return false;
    }
    s_restart = true;
    return true;
}

} // namespace

std::string family()
{
    std::string value;
    return config("GetStr", kFamilyConfigKey, "", &value) ? value : std::string();
}

bool sharp()
{
    std::string value;
    return config("GetInt", kSharpConfigKey, "0", &value) && value == "1";
}

void apply() { launcher_fonts().set_ui_font(family(), sharp()); }

bool set_family(const std::string &value)
{
    return value == family() || store("SetStr", kFamilyConfigKey, value, family());
}

bool set_sharp(bool value)
{
    return store("SetInt", kSharpConfigKey, value ? "1" : "0", sharp() ? "1" : "0");
}

bool restart_requested() { return s_restart; }

} // namespace launcher_ui_font
