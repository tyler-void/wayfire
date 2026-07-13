// spread-overview per-cell wallpaper — the ONLY image-file load + texture-upload contact
// (constitution Principle II). Isolated in its own translation unit so that if a restart
// crashes on overview-open, THIS file is the only new suspect (US-wallpaper isolation).
//
// WHY THIS INSTEAD OF LIVE CAPTURE (ADR-002 rationale, recorded in research.md): capturing
// the live BACKGROUND scene layer into an auxilliary_buffer_t via a render pass is the
// project's highest GPU crash-surface — on a multi-GPU (Intel iGPU + NVIDIA dGPU) box with
// explicit sync it risks buffer-ownership / format / frame-timing faults. The wallpaper is
// a STATIC FILE, so we load it directly: cairo decodes the PNG, we normalize to ARGB32,
// and owned_texture_t uploads it via wlr_texture_from_pixels — the SAME renderer-agnostic
// path the border/label/dim overlay nodes already use. No render pass, no aux buffer, no
// cross-GPU transfer; just a plain pixel upload.
//
// FAIL-SOFT CONTRACT (mandatory — never fail hard): unset path -> disabled; missing file /
// decode failure / bad size / texture-upload failure -> log + leave wallpaper_ok=false so
// the overview falls back to today's look (cells show the existing full-screen wallpaper
// behind them). NEVER aborts the compositor.
//
// B1 SCOPE: load + upload + report dimensions. Draws NOTHING (the per-cell blit is B2).

#include "overview.hpp"

#include <wayfire/util/log.hpp>
#include <cairo/cairo.h>

namespace wf
{
namespace spread
{
void spread_overview_t::load_wallpaper()
{
    wallpaper_ok   = false;
    wallpaper_size = {0, 0};

    const std::string path = opt_wallpaper_path;
    if (path.empty())
    {
        LOGI("spread-overview: wallpaper disabled (wallpaper_path unset)");
        return;
    }

    // 1. Decode the PNG from disk. cairo's built-in loader never aborts — a missing file
    //    or a non-PNG yields a surface in an error status, which we detect and degrade on.
    cairo_surface_t *png = cairo_image_surface_create_from_png(path.c_str());
    const cairo_status_t st = cairo_surface_status(png);
    if (st != CAIRO_STATUS_SUCCESS)
    {
        LOGE("spread-overview: wallpaper load failed for '", path, "': ",
            cairo_status_to_string(st), " -> feature disabled (no crash)");
        cairo_surface_destroy(png); // destroying an error-surface is safe
        return;
    }

    const int w = cairo_image_surface_get_width(png);
    const int h = cairo_image_surface_get_height(png);
    if ((w <= 0) || (h <= 0))
    {
        LOGE("spread-overview: wallpaper '", path, "' has invalid size ", w, "x", h,
            " -> feature disabled");
        cairo_surface_destroy(png);
        return;
    }

    // 2. Normalize to ARGB32. owned_texture_t only accepts ARGB32 (it asserts otherwise),
    //    and a PNG without an alpha channel decodes to RGB24 — so re-paint onto a fresh
    //    ARGB32 surface. Bulletproof regardless of the source's channel count.
    cairo_surface_t *argb = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(argb) != CAIRO_STATUS_SUCCESS)
    {
        LOGE("spread-overview: wallpaper ARGB32 alloc failed -> feature disabled");
        cairo_surface_destroy(argb);
        cairo_surface_destroy(png);
        return;
    }

    cairo_t *cr = cairo_create(argb);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, png, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(argb);

    // 3. Upload to a renderer texture. This is the only GPU-touching step: a plain pixel
    //    upload (wlr_texture_from_pixels inside owned_texture_t). On failure the wlr_texture
    //    is NULL and from_texture wraps it without dereferencing (verified: render.cpp),
    //    so get_wlr_texture()==NULL is a clean, non-crashing failure signal.
    wf::owned_texture_t tex{argb};
    cairo_surface_destroy(argb);
    cairo_surface_destroy(png);

    auto wtex = tex.get_texture();
    if (!wtex || !wtex->get_wlr_texture())
    {
        LOGE("spread-overview: wallpaper texture upload failed for '", path,
            "' -> feature disabled (no crash)");
        return;
    }

    wallpaper_tex  = std::move(tex);
    wallpaper_size = {w, h};
    wallpaper_ok   = true;
    LOGI("spread-overview: wallpaper loaded '", path, "' ", w, "x", h,
        " -> texture OK  [B1: uploaded, not drawn yet]");
}
} // namespace spread
} // namespace wf
