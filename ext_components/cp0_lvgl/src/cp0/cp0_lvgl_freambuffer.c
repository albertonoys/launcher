/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "hal_lvgl_bsp.h"
#include "lvgl/lvgl.h"
#include "commount.h"
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cp0_lvgl.h"
#include "../cp0_display_screenshot_contract.hpp"
#include <pthread.h>

static int find_st7789v_fbdev(char *dev_path, size_t buf_size)
{
    if (dev_path == NULL || buf_size == 0)
        return -1;

    if (access("/dev/fb_lcd", F_OK) == 0) {
        snprintf(dev_path, buf_size, "/dev/fb_lcd");
        return 0;
    }

    FILE *fp = fopen("/proc/fb", "r");
    if (fp != NULL) {
        char line[256];
        int fb_num = -1;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (strstr(line, "fb_st7789v") != NULL || strstr(line, "st7789") != NULL) {
                if (sscanf(line, "%d", &fb_num) == 1)
                    break;
            }
        }
        fclose(fp);
        if (fb_num >= 0) {
            snprintf(dev_path, buf_size, "/dev/fb%d", fb_num);
            return 0;
        }
    }

    snprintf(dev_path, buf_size, "/dev/fb0");
    return 0;
}

/* The panel's device node exists before udev has given it its group/mode, and the launcher runs as a
 * user, so the first open can fail with EACCES and LVGL then falls back to a dummy 800x480 display.
 * Wait until it is openable (todo #25). */
static bool wait_for_fbdev(const char *dev, int tries, int interval_ms)
{
    for (int i = 0; i < tries; ++i) {
        int fd = open(dev, O_RDWR);
        if (fd >= 0) {
            close(fd);
            return true;
        }
        if (i == 0)
            fprintf(stderr, "[BOOT] %s not openable yet: %s\n", dev, strerror(errno));
        usleep(interval_ms * 1000);
    }
    fprintf(stderr, "[BOOT] %s still not openable after %d ms\n", dev, tries * interval_ms);
    return false;
}

/* The backlight node's power-supply regulator powers the panel, so a session that ended with the screen
 * off (or with the panel blanked) can leave it latched blank: the DRM probe re-runs the panel's init but
 * a plain FBIOBLANK(UNBLANK) on an already-unblanked fb is a no-op, so the panel stays black while the
 * launcher draws into the framebuffer. Force one off -> on cycle once the fb is open, before the first
 * frame (todo #36). */
static void panel_resync(const char *device)
{
    char name[32] = "fb0";
    char target[32] = {0};
    const char *base = strrchr(device, '/');
    base = base ? base + 1 : device;
    if (readlink(device, target, sizeof(target) - 1) > 0)
        snprintf(name, sizeof(name), "%s", target);
    else if (strncmp(base, "fb", 2) == 0)
        snprintf(name, sizeof(name), "%s", base);

    char path[96];
    snprintf(path, sizeof(path), "/sys/class/graphics/%s/blank", name);
    for (int pass = 0; pass < 2; ++pass) {
        int fd = open(path, O_WRONLY | O_CLOEXEC);
        if (fd < 0)
            return;
        const char value = pass == 0 ? '1' : '0';
        (void)!write(fd, &value, 1);
        close(fd);
        if (pass == 0)
            usleep(20 * 1000);
    }
}

void init_freambuffer_disp()
{
    static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&init_mutex);
    if (lv_display_get_default() != NULL) {
        pthread_mutex_unlock(&init_mutex);
        return;
    }
#if LV_USE_LINUX_DRM
    const char *device = getenv("LV_LINUX_DRM_DEVICE");
    char *detected_device = NULL;
    if (device == NULL || device[0] == '\0') {
        detected_device = lv_linux_drm_find_device_path();
        device = detected_device;
    }

    if (device == NULL || device[0] == '\0') {
        fprintf(stderr, "Failed to find a connected DRM device\n");
        lv_free(detected_device);
        pthread_mutex_unlock(&init_mutex);
        return;
    }

    printf("Using DRM device: %s\n", device);
    lv_display_t *disp = lv_linux_drm_create();
    if (disp == NULL) {
        fprintf(stderr, "Failed to create DRM display\n");
        lv_free(detected_device);
        pthread_mutex_unlock(&init_mutex);
        return;
    }

    if (lv_linux_drm_set_file(disp, device, -1) != LV_RESULT_OK) {
        fprintf(stderr, "Failed to initialize DRM device: %s\n", device);
        lv_display_delete(disp);
        lv_free(detected_device);
        pthread_mutex_unlock(&init_mutex);
        return;
    }

    lv_free(detected_device);

    printf("DRM resolution: %dx%d\n",
           (int)lv_display_get_horizontal_resolution(disp),
           (int)lv_display_get_vertical_resolution(disp));
#else
    lv_display_t *disp = lv_linux_fbdev_create();
    if (disp == NULL) {
        printf("Failed to create fbdev display!\n");
        pthread_mutex_unlock(&init_mutex);
        return;
    }

    const char *device = getenv("LV_LINUX_FBDEV_DEVICE");
    char fbdev[64] = {0};
    if (device == NULL || device[0] == '\0') {
        find_st7789v_fbdev(fbdev, sizeof(fbdev));
        device = fbdev;
    }

    setenv("APPLAUNCH_LINUX_FBDEV_DEVICE", device, 1);
    setenv("LV_LINUX_FBDEV_DEVICE", device, 1);
    printf("Using framebuffer device: %s\n", device);
    wait_for_fbdev(device, 60, 50);
    lv_linux_fbdev_set_file(disp, device);
    panel_resync(device);
    if(0)
        lv_linux_fbdev_set_force_refresh(disp, true);

    lv_coord_t w = lv_display_get_horizontal_resolution(disp);
    lv_coord_t h = lv_display_get_vertical_resolution(disp);
    printf("Framebuffer resolution: %dx%d\n", w, h);
#endif
    pthread_mutex_unlock(&init_mutex);
}
