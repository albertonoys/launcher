/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "launcher_uninstall_model.hpp"

#include "json.hpp"

#include <sstream>

std::string launcher_uninstall_find_app_id(const std::string &installed_json,
                                           const std::string &desktop_path)
{
    if (desktop_path.empty()) return {};
    const nlohmann::json records = nlohmann::json::parse(installed_json, nullptr, false);
    if (!records.is_object()) return {};
    for (auto it = records.begin(); it != records.end(); ++it) {
        if (!it.value().is_object()) continue;
        const auto files = it.value().find("files");
        if (files == it.value().end() || !files->is_array()) continue;
        for (const auto &file : *files)
            if (file.is_string() && file.get<std::string>() == desktop_path) return it.key();
    }
    return {};
}

bool launcher_uninstall_parse_job(const std::string &output, LauncherUninstallJob &job)
{
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("PACKAGE_JOB\t", 0) != 0) continue;
        std::vector<std::string> fields;
        std::string field;
        std::istringstream parts(line);
        while (std::getline(parts, field, '\t')) fields.push_back(field);
        if (fields.size() < 7 || fields[1] != "uninstall" || fields[2].empty() || fields[5].empty())
            return false;
        job = {fields[2], fields[5], fields[6]};
        return true;
    }
    return false;
}

std::string launcher_uninstall_first_error(const std::string &output)
{
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("ERROR\t", 0) != 0) continue;
        const size_t end = line.find('\t', 6);
        return line.substr(6, end == std::string::npos ? std::string::npos : end - 6);
    }
    return {};
}

std::vector<std::string> launcher_uninstall_helper_argv(const std::string &backend,
                                                        const LauncherUninstallJob &job)
{
    return {backend, "--package-helper", "uninstall", "--package-value", job.package,
            "--package-transaction", job.transaction_id, "--package-pending-path", job.pending_path};
}
