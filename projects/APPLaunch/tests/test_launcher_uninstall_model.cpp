/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "../main/ui/model/launcher_uninstall_model.hpp"

#include <cassert>

int main()
{
    const std::string desktop = "/usr/share/APPLaunch/applications/foo.desktop";
    const std::string installed = R"({
        "bar": {"package": "bar-pkg", "files": ["/usr/bin/bar", "/usr/share/APPLaunch/applications/bar.desktop"]},
        "foo": {"package": "foo-pkg", "files": ["/usr/bin/foo", "/usr/share/APPLaunch/applications/foo.desktop"]},
        "odd": "not an object", "nofiles": {"package": "x"}, "badfiles": {"files": [1, null]}})";
    assert(launcher_uninstall_find_app_id(installed, desktop) == "foo");
    assert(launcher_uninstall_find_app_id(installed, "/usr/share/APPLaunch/applications/none.desktop").empty());
    assert(launcher_uninstall_find_app_id(installed, "").empty());
    assert(launcher_uninstall_find_app_id("not json", desktop).empty());
    assert(launcher_uninstall_find_app_id("[]", desktop).empty());
    assert(launcher_uninstall_find_app_id("", desktop).empty());

    LauncherUninstallJob job;
    const std::string line = "PACKAGE_JOB\tuninstall\tfoo-pkg\t0\t\ttx1\t/home/pi/.local/share/x/pending-package.json\n";
    assert(launcher_uninstall_parse_job("PROGRESS\tx\n" + line, job));
    assert(job.package == "foo-pkg" && job.transaction_id == "tx1");
    assert(job.pending_path == "/home/pi/.local/share/x/pending-package.json");
    assert(!launcher_uninstall_parse_job("", job));
    assert(!launcher_uninstall_parse_job("ERROR\tboom\t1\n", job));
    assert(!launcher_uninstall_parse_job("PACKAGE_JOB\tinstall\tfoo.deb\t0\t\ttx\tp\n", job));
    assert(!launcher_uninstall_parse_job("PACKAGE_JOB\tuninstall\tfoo-pkg\t0\t\t\tp\n", job));
    assert(!launcher_uninstall_parse_job("PACKAGE_JOB\tuninstall\tfoo-pkg\n", job));

    assert(launcher_uninstall_first_error("PENDING_CONFLICT\ta\tinstall\tp\nERROR\tbusy now\t1\nERROR\tlater\n") == "busy now");
    assert(launcher_uninstall_first_error("ERROR\tno code\n") == "no code");
    assert(launcher_uninstall_first_error("UNINSTALLED\tfoo\n").empty());

    const auto argv = launcher_uninstall_helper_argv("/b", {"foo-pkg", "tx1", "/p"});
    assert((argv == std::vector<std::string>{"/b", "--package-helper", "uninstall", "--package-value", "foo-pkg",
                                             "--package-transaction", "tx1", "--package-pending-path", "/p"}));
    return 0;
}
