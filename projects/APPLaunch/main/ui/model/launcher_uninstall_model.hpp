/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>
#include <vector>

// Pure helpers for deleting a store-installed app through the App Store backend.

// What the backend's `--prepare-package uninstall` hands to the root helper.
struct LauncherUninstallJob {
    std::string package;
    std::string transaction_id;
    std::string pending_path;
};

// installed.json is {app_id: {package, files: [...]}}. Returns the app_id whose files
// contain `desktop_path` (exact match), or "" when there is none or the JSON is invalid.
std::string launcher_uninstall_find_app_id(const std::string &installed_json,
                                           const std::string &desktop_path);

// First `PACKAGE_JOB\tuninstall\t<package>\t0\t\t<tx>\t<pending_path>` line of the output.
bool launcher_uninstall_parse_job(const std::string &output, LauncherUninstallJob &job);

// Message of the first `ERROR\t<message>` line, or "".
std::string launcher_uninstall_first_error(const std::string &output);

// Root helper command line; mirrors appstore::privileged_package_argv for an uninstall.
std::vector<std::string> launcher_uninstall_helper_argv(const std::string &backend,
                                                        const LauncherUninstallJob &job);
