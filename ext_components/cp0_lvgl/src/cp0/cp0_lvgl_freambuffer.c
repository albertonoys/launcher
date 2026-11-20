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
#include <linux/fb.h>
#include <sys/ioctl.h>
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

/* This driver's deferred IO never reports the page faults of LVGL's framebuffer mmap on this panel,
 * so a frame written with memcpy is never pushed to the glass: only full repaints (fbcon, a
 * blank/unblank cycle, a write()) ever arrive. Write the flushed rows with pwrite() instead - fb_write
 * reports the exact damage range, so the panel follows the UI and only the dirty area is sent over
 * SPI. force_refresh would push the whole frame on every flush and makes the UI lag. (todo #36) */
static int s_flush_fd = -1;
static uint32_t s_fb_line_length = 0;

static void pwrite_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p)
{
    if (s_flush_fd < 0 || s_fb_line_length == 0) {
        lv_display_flush_ready(disp);
        return;
    }
    const lv_color_format_t cf = lv_display_get_color_format(disp);
    const uint32_t px_size = lv_color_format_get_size(cf);
    const int32_t w = lv_area_get_width(area);
    const size_t row_bytes = (size_t)w * px_size;
    const uint32_t src_stride = lv_draw_buf_width_to_stride(w, cf);
    off_t pos = (off_t)area->y1 * s_fb_line_length + (off_t)area->x1 * px_size;
    for (int32_t y = area->y1; y <= area->y2; ++y) {
        (void)!pwrite(s_flush_fd, color_p, row_bytes, pos);
        color_p += src_stride;
        pos += s_fb_line_length;
    }
    lv_display_flush_ready(disp);
}

static void use_pwrite_flush(lv_display_t *disp, const char *device)
{
    struct fb_fix_screeninfo fix;
    int fd = open(device, O_RDWR);
    if (fd < 0) return;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0 || fix.line_length == 0) {
        close(fd);
        return;
    }
    s_flush_fd = fd;
    s_fb_line_length = fix.line_length;
    lv_display_set_flush_cb(disp, pwrite_flush_cb);
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
    use_pwrite_flush(disp, device);
    /* LVGL writes the frame with write() (LV_LINUX_FBDEV_MMAP=n), which goes through fb_write and
     * reports the exact damage range; the mmap memcpy path never pushes anything here because this
     * driver's deferred IO does not see those page faults. force_refresh would push the whole frame
     * on every flush, which is what made the UI laggy. */
    if(0)
        lv_linux_fbdev_set_force_refresh(disp, true);

    lv_coord_t w = lv_display_get_horizontal_resolution(disp);
    lv_coord_t h = lv_display_get_vertical_resolution(disp);
    printf("Framebuffer resolution: %dx%d\n", w, h);
#endif
    pthread_mutex_unlock(&init_mutex);
}
