/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cp0_lvgl_app_runner.hpp"
#include "sample_log.h"
#include "ui/launcher_media_controls.h"
#include "ui/ui.h"
#include "ui/ui_screensaver.h"

#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>

#if CONFIG_BACKWARD_CPP_ENABLED
#define BACKWARD_HAS_DW 1
#include "backward.hpp"
#include "backward.h"
#endif

const char *bash_init = "if [ ! -f ~/Downloads/hello.md ] ; then unzip /var/template.zip -d ~/Downloads/ ; sync ; fi";

/* Boot-time attribution (docs/boot.md): CP0_INIT_TIMING=1 logs the phases around the init steps. */
static void tlog(const char *what)
{
    static struct timespec t0;
    struct timespec t;
    if (!getenv("CP0_INIT_TIMING")) return;
    clock_gettime(CLOCK_MONOTONIC, &t);
    if (!t0.tv_sec) t0 = t;
    fprintf(stderr, "[init] %-16s %7.1f ms\n", what,
        (t.tv_sec - t0.tv_sec) * 1000.0 + (t.tv_nsec - t0.tv_nsec) / 1e6);
    fflush(stderr);
}

int main(void)
{
    tlog("main");
    system(bash_init);
    tlog("after unzip-check");
    Cp0LvglRunOptions options;
    options.after_resource_init = []() {
        cp0_signal_settings_api({"GpioSet", "BACKLIGHT", "0"}, [](int code, std::string data) {
            if (code == 0)
                SLOGI("[BOOT] set m5ioe1 line 9 low");
            else
                SLOGE("[BOOT] failed to set m5ioe1 line 9 low: %s", data.c_str());
        });
        if (launcher_media_controls::restore_startup_backlight())
            SLOGI("[BOOT] restored working backlight brightness");
        else
            SLOGE("[BOOT] failed to restore working backlight brightness");
    };
    options.setup = []() {
        SLOGI("[BOOT] cp0_lvgl initialized");
        launcher_ui::init();
        ui_screensaver_init();
        return true;
    };
    options.teardown = []() { launcher_ui::deinit(); };
    tlog("before run");
    const int rc = cp0_lvgl_run(std::move(options));
    tlog("after run");
    return rc;
}
