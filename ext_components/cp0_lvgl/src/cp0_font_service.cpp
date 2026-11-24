/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "cp0_font_service.hpp"

#include "cp0_font_cache_policy.hpp"
#include "cp0_sync_signal.hpp"
#include "hal_lvgl_bsp.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <utility>

extern "C" {
LV_FONT_DECLARE(cp0_montserrat_medium_latin_10)
LV_FONT_DECLARE(cp0_montserrat_medium_latin_12)
LV_FONT_DECLARE(cp0_montserrat_medium_latin_14)
}

namespace {

void set_glyph_fallback(lv_font_t &font, const lv_font_t &fallback)
{
    // LVGL positions a fallback glyph against the primary font's baseline.
    const int ascent = std::max(font.line_height - font.base_line,
                                fallback.line_height - fallback.base_line);
    font.base_line = std::max(font.base_line, fallback.base_line);
    font.line_height = ascent + font.base_line;
    font.fallback = &fallback;
}

template<const lv_font_t &Primary, const lv_font_t &Latin>
lv_font_t *montserrat_with_latin()
{
    static lv_font_t font = [] {
        lv_font_t copy = Primary;
        set_glyph_fallback(copy, Latin);
        return copy;
    }();
    return &font;
}

const auto kRenderMono = static_cast<lv_freetype_font_render_mode_t>(2);

bool is_ui_font(const std::string &name)
{
    return name == "AlibabaPuHuiTi-3-55-Regular.ttf" || name == "DejaVuSans.ttf" ||
           name == "Montserrat-Bold.ttf";
}

struct UiFace
{
    const char *file;
    uint16_t size;            // 0: any size
    const char *bold = nullptr; // a drawn bold, used instead of FreeType's synthetic one
};

#define GREYBEARD(px) {"Greybeard-" #px "px.ttf", px, "Greybeard-" #px "px-Bold.ttf"}

// Pixel fonts are only sharp at their native sizes, so a size snaps to the nearest one.
const UiFace *ui_font_face(const std::string &family, uint16_t size)
{
    static const UiFace dejavu[] = {{"DejaVuSans.ttf", 0}};
    static const UiFace terminus[] = {
        {"TerminusTTF-4.49.3.ttf", 12}, {"TerminusTTF-4.49.3.ttf", 14},
        {"TerminusTTF-4.49.3.ttf", 16}, {"TerminusTTF-4.49.3.ttf", 18},
        {"TerminusTTF-4.49.3.ttf", 20}, {"TerminusTTF-4.49.3.ttf", 22},
        {"TerminusTTF-4.49.3.ttf", 24}};
    static const UiFace tamzen[] = {
        {"Tamzen5x9r.ttf", 9}, {"Tamzen6x12r.ttf", 12}, {"Tamzen7x13r.ttf", 13},
        {"Tamzen7x14r.ttf", 14}, {"Tamzen8x15r.ttf", 15}, {"Tamzen8x16r.ttf", 16},
        {"Tamzen10x20r.ttf", 20}};
    static const UiFace spleen[] = {
        {"spleen-6x12.otf", 12}, {"spleen-8x16.otf", 16}, {"spleen-12x24.otf", 24}};
    static const UiFace greybeard[] = {
        GREYBEARD(11), GREYBEARD(12), GREYBEARD(13), GREYBEARD(14), GREYBEARD(15),
        GREYBEARD(16), GREYBEARD(17), GREYBEARD(18), GREYBEARD(22)};
    static const UiFace ark[] = {{"ark-pixel-12px-proportional-latin.ttf", 12},
                                 {"ark-pixel-12px-proportional-latin.ttf", 24}};
    static const UiFace cozette[] = {{"CozetteVector.ttf", 13}, {"CozetteVector.ttf", 26}};
    static const UiFace proggy[] = {{"ProggyClean.ttf", 16}};
    static const UiFace departure[] = {{"DepartureMono-Regular.otf", 11}};
    static const UiFace scientifica[] = {{"scientifica.ttf", 11, "scientificaBold.ttf"},
                                         {"scientifica.ttf", 22, "scientificaBold.ttf"}};

    const UiFace *begin = nullptr, *end = nullptr;
#define UI_FAMILY(name) if (family == #name) begin = std::begin(name), end = std::end(name);
    UI_FAMILY(dejavu) UI_FAMILY(terminus) UI_FAMILY(tamzen) UI_FAMILY(spleen)
    UI_FAMILY(greybeard) UI_FAMILY(ark) UI_FAMILY(cozette) UI_FAMILY(proggy)
    UI_FAMILY(departure) UI_FAMILY(scientifica)
#undef UI_FAMILY
    if (!begin) return nullptr;

    const UiFace *best = begin;
    for (const UiFace *face = begin; face != end; ++face) {
        if (std::abs(face->size - size) < std::abs(best->size - size)) best = face;
    }
    return best;
}

#undef GREYBEARD

} // namespace

void Cp0FontService::set_ui_font(std::string family, bool sharp)
{
    ui_family_ = std::move(family);
    ui_sharp_ = sharp;
}

lv_font_t *Cp0FontService::status(uint16_t size)
{
    return ui_family_.empty() ? fallback(size) : get("DejaVuSans.ttf", size);
}

Cp0FontService::~Cp0FontService()
{
    release();
}

lv_font_t *Cp0FontService::get(const char *font_name, uint16_t size,
                               lv_freetype_font_style_t style,
                               lv_freetype_font_render_mode_t render_mode,
                               const lv_font_t *glyph_fallback, GlyphRange glyph_range)
{
#if LV_USE_FREETYPE
    if (font_name && is_ui_font(font_name)) {
        const bool sharp = ui_sharp_ && render_mode == LV_FREETYPE_FONT_RENDER_MODE_BITMAP &&
                           glyph_range == GlyphRange::All;
        lv_font_t *font = nullptr;
        if (sharp) {
            // 1-bit only for text: symbols such as the favorite star break up without anti-aliasing.
            lv_font_t *smooth = get_cached(font_name, size, style, render_mode, glyph_fallback, glyph_range);
            font = get_cached(font_name, size, style, kRenderMono, smooth, GlyphRange::Text);
            if (font == fallback(size)) font = smooth;
        } else {
            font = get_cached(font_name, size, style, render_mode, glyph_fallback, glyph_range);
        }
        const UiFace *face = glyph_range == GlyphRange::All ? ui_font_face(ui_family_, size) : nullptr;
        if (!face || std::string(face->file) == font_name) return font;
        const uint16_t ui_size = face->size ? face->size : size;
        const char *ui_file = face->file;
        auto ui_style = style;
        const bool bold = (style & LV_FREETYPE_FONT_STYLE_BOLD) || std::string(font_name) == "Montserrat-Bold.ttf";
        if (bold && face->bold) {
            ui_file = face->bold;
            ui_style = static_cast<lv_freetype_font_style_t>(style & ~LV_FREETYPE_FONT_STYLE_BOLD);
        }
        // The original font stays behind it for glyphs it lacks (CJK, symbols).
        lv_font_t *ui_font = sharp ? get_cached(ui_file, ui_size, ui_style, kRenderMono, font, GlyphRange::Text)
                                   : get_cached(ui_file, ui_size, ui_style, render_mode, font, glyph_range);
        return ui_font == fallback(ui_size) ? font : ui_font;
    }
#endif
    return get_cached(font_name, size, style, render_mode, glyph_fallback, glyph_range);
}

#if LV_USE_FREETYPE
lv_font_t *Cp0FontService::get_cached(const char *font_name, uint16_t size,
                                      lv_freetype_font_style_t style,
                                      lv_freetype_font_render_mode_t render_mode,
                                      const lv_font_t *glyph_fallback, GlyphRange glyph_range)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string path = resolve_path(font_name);
    if (path.empty()) return fallback(size);

    const std::string font_key = key(path, size, style, render_mode, glyph_fallback, glyph_range);
    const auto found = fonts_.find(font_key);
    if (found != fonts_.end()) return found->second.font;

    lv_font_t *font = lv_freetype_font_create(path.c_str(), render_mode, size, style);
    if (!cp0::font::should_cache(font)) return fallback(size);
    if (glyph_fallback) set_glyph_fallback(*font, *glyph_fallback);
    try {
        auto entry = fonts_.emplace(font_key, CachedFont{font, font->get_glyph_dsc}).first;
        if (glyph_range != GlyphRange::All) {
            // The cached callback's address stays valid across rehashes.
            font->user_data = &entry->second.get_glyph_dsc;
        }
        if (glyph_range == GlyphRange::LatinExtendedA) {
            font->get_glyph_dsc = [](const lv_font_t *face, lv_font_glyph_dsc_t *dsc,
                                     uint32_t letter, uint32_t next) {
                if (letter < 0x0100 || letter > 0x017F) return false;
                const auto get_glyph_dsc = *static_cast<const GlyphCallback *>(face->user_data);
                return get_glyph_dsc(face, dsc, letter, next);
            };
        } else if (glyph_range == GlyphRange::Text) {
            font->get_glyph_dsc = [](const lv_font_t *face, lv_font_glyph_dsc_t *dsc,
                                     uint32_t letter, uint32_t next) {
                if (letter >= 0x2000) return false;
                const auto get_glyph_dsc = *static_cast<const GlyphCallback *>(face->user_data);
                return get_glyph_dsc(face, dsc, letter, next);
            };
        }
    } catch (...) {
        lv_freetype_font_delete(font);
        return fallback(size);
    }
    return font;
}
#else
lv_font_t *Cp0FontService::get_cached(const char *, uint16_t size, lv_freetype_font_style_t,
                                      lv_freetype_font_render_mode_t, const lv_font_t *,
                                      GlyphRange)
{
    return fallback(size);
}
#endif

lv_font_t *Cp0FontService::get_mono(const char *font_name, uint16_t size,
                                    lv_freetype_font_style_t style)
{
    return get(font_name, size, style,
               static_cast<lv_freetype_font_render_mode_t>(2));
}

#if LV_USE_FREETYPE
std::string Cp0FontService::resolve_path(const char *font_name)
{
    if (!font_name || !font_name[0]) return {};
    std::string value(font_name);
    if (value.find('/') != std::string::npos ||
        (value.size() > 1 && value[1] == ':')) return value;

    std::string resolved;
    cp0::signal::invoke_noexcept([&] { cp0_signal_filesystem_api({"Path", value}, [&](int code, std::string data) {
        if (code == 0) resolved = std::move(data);
    }); });
    return resolved.empty() ? value : resolved;
}

std::string Cp0FontService::key(const std::string &path, uint16_t size,
                                lv_freetype_font_style_t style,
                                lv_freetype_font_render_mode_t render_mode,
                                const lv_font_t *glyph_fallback, GlyphRange glyph_range)
{
    return path + "#" + std::to_string(size) + "#" +
           std::to_string(static_cast<int>(style)) + "#" +
           std::to_string(static_cast<int>(render_mode)) + "#" +
           std::to_string(reinterpret_cast<std::uintptr_t>(glyph_fallback)) +
           (glyph_range == GlyphRange::LatinExtendedA ? "#latin-extended-a"
            : glyph_range == GlyphRange::Text        ? "#text"
                                                      : "");
}
#endif

lv_font_t *Cp0FontService::fallback(uint16_t size) const
{
#if LV_FONT_MONTSERRAT_20
    if (size >= 18) return const_cast<lv_font_t *>(&lv_font_montserrat_20);
#endif
#if LV_FONT_MONTSERRAT_14
    if (size >= 14) return montserrat_with_latin<lv_font_montserrat_14,
                                                  cp0_montserrat_medium_latin_14>();
#endif
#if LV_FONT_MONTSERRAT_12
    if (size >= 12) return montserrat_with_latin<lv_font_montserrat_12,
                                                  cp0_montserrat_medium_latin_12>();
#endif
#if LV_FONT_MONTSERRAT_10
    (void)size;
    return montserrat_with_latin<lv_font_montserrat_10, cp0_montserrat_medium_latin_10>();
#else
    (void)size;
    return const_cast<lv_font_t *>(LV_FONT_DEFAULT);
#endif
}

void Cp0FontService::release()
{
#if LV_USE_FREETYPE
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &font : fonts_) {
        lv_freetype_font_delete(font.second.font);
    }
    fonts_.clear();
#endif
}

Cp0FontService &cp0_fonts()
{
    static Cp0FontService service;
    return service;
}
