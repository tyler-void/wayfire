# ADR-002: per-cell wallpaper loads the static file, not a live scene capture

**Status:** Accepted
**Date:** 2026-07-13
**Feature:** 001-spread-overview
**Referenced by:** `plugins/spread-overview/src/wallpaper.cpp` (header comment), `overview.hpp`

## Context

Flavor B of the overview draws the real desktop wallpaper, scaled, into each workspace cell
(expo-style tiles) below the thumbnails. Two ways to obtain that texture were considered:

1. **Live capture.** Render the BACKGROUND scene layer into a `wf::auxilliary_buffer_t` via a
   `render_pass_t` and reuse the resulting texture. This is the project's **highest GPU
   crash-surface** — on the target machine (hybrid **Intel iGPU + NVIDIA dGPU** with explicit
   sync) it risks buffer-ownership / format / frame-timing faults handing a buffer between
   renderers. It is also entangled with per-frame timing. Expo's proven `workspace_stream_node_t`
   is not reusable here either: it renders the **entire** workspace including its windows, which
   would double the thumbnails.
2. **File load.** The wallpaper is a **static file** (the user sets it via `awww`). Loading a PNG
   into a texture is a standard, well-trodden operation with no cross-GPU concerns.

## Decision

Load the wallpaper **file** directly. `wallpaper.cpp` decodes the PNG with cairo, normalizes it to
ARGB32, and uploads it **once** through `wf::owned_texture_t` (`wlr_texture_from_pixels`) — the
same renderer-agnostic path the border / label / dim overlay nodes already use. The
`wallpaper_node_t` blits it **cover-cropped** (`texture_t::set_source_box`) into each cell. No
render pass, no auxiliary buffer, no buffer crossing between the Intel and NVIDIA renderers — just a
plain CPU-pixels→texture upload.

The user points `wallpaper_path` at the same image file their wallpaper daemon uses (the plugin
cannot read the daemon's config). The feature is **fully fail-soft**: unset path / missing file /
decode failure / upload failure → no per-cell wallpaper (the real desktop shows through), **never a
crash**.

## Consequences

**Upsides:**
- Sidesteps the cross-GPU capture crash-surface entirely — the decisive reason on this hardware.
- One-time, deterministic upload; renderer-agnostic (works under GL or Vulkan); consistent with the
  existing overlay nodes; isolated in one translation unit (Principle II).

**Costs:**
- The plugin can't auto-discover the wallpaper; the user must set `wallpaper_path`. PNG only in v1.
- A dynamic / animated / per-output wallpaper daemon is not reflected — the tile is a static file.
- If the daemon crops the image differently than our cover-crop-to-cell-aspect, tiles can differ
  slightly from the live desktop.

**Revisit if:** we need live or animated backgrounds, or per-output wallpapers — a guarded,
single-GPU capture path could then be added behind the same `wallpaper_node_t`, gated so a capture
failure still degrades to the file (or to nothing) rather than crashing.

## Affected artifacts
- `plugins/spread-overview/src/wallpaper.cpp` — the only image-file / texture-upload contact.
- `overlay.{hpp,cpp}` (`wallpaper_node_t`), `render.cpp` (wiring), `metadata/spread-overview.xml`
  (`wallpaper_path`), `research.md`.
