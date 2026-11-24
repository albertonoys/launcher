/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl/lvgl.h"

#if LV_USE_FREETYPE
#include "lvgl/src/libs/freetype/lv_freetype.h"
#else
#ifndef LV_FREETYPE_FONT_STYLE_NORMAL
typedef int lv_freetype_font_style_t;
#define LV_FREETYPE_FONT_STYLE_NORMAL 0
#define LV_FREETYPE_FONT_STYLE_ITALIC (1 << 0)
#define LV_FREETYPE_FONT_STYLE_BOLD (1 << 1)
#endif
#ifndef LV_FREETYPE_FONT_RENDER_MODE_BITMAP
typedef int lv_freetype_font_render_mode_t;
#define LV_FREETYPE_FONT_RENDER_MODE_BITMAP 0
#define LV_FREETYPE_FONT_RENDER_MODE_OUTLINE 1
#endif
#endif

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class Cp0FontService
{
public:
    Cp0FontService() = default;
    ~Cp0FontService();

    Cp0FontService(const Cp0FontService &) = delete;
    Cp0FontService &operator=(const Cp0FontService &) = delete;

    // Text: below U+2000, so symbols, arrows and CJK fall through to the fallback.
    enum class GlyphRange { All, LatinExtendedA, Text };

    // The optional glyph fallback must stay alive until release().
    lv_font_t *get(const char *font_name, uint16_t size,
                   lv_freetype_font_style_t style = LV_FREETYPE_FONT_STYLE_NORMAL,
                   lv_freetype_font_render_mode_t render_mode = LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                   const lv_font_t *glyph_fallback = nullptr,
                   GlyphRange glyph_range = GlyphRange::All);
    lv_font_t *get_mono(const char *font_name, uint16_t size,
                        lv_freetype_font_style_t style = LV_FREETYPE_FONT_STYLE_NORMAL);

    lv_font_t *fallback(uint16_t size) const;
    void release();

    // Launcher setting, applied before any font is created: family ("dejavu",
    // "terminus", "tamzen", "spleen", "greybeard", "ark", "cozette", "proggy",
    // "departure", "scientifica"; "" keeps them) replaces the UI sans fonts,
    // and sharp draws them 1-bit instead of anti-aliased.
    void set_ui_font(std::string family, bool sharp);
    // Status bar text: the built-in bitmap font unless a UI font family was set.
    lv_font_t *status(uint16_t size);

private:
    std::string ui_family_;
    bool ui_sharp_ = false;
    lv_font_t *get_cached(const char *font_name, uint16_t size, lv_freetype_font_style_t style,
                          lv_freetype_font_render_mode_t render_mode,
                          const lv_font_t *glyph_fallback, GlyphRange glyph_range);
#if LV_USE_FREETYPE
    using GlyphCallback = decltype(lv_font_t::get_glyph_dsc);
    struct CachedFont
    {
        lv_font_t *font;
        GlyphCallback get_glyph_dsc;
    };

    static std::string resolve_path(const char *font_name);
    static std::string key(const std::string &path, uint16_t size,
                           lv_freetype_font_style_t style,
                           lv_freetype_font_render_mode_t render_mode,
                           const lv_font_t *glyph_fallback, GlyphRange glyph_range);
    std::unordered_map<std::string, CachedFont> fonts_;
    std::mutex mutex_;
#endif
};

Cp0FontService &cp0_fonts();
