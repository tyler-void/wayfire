#pragma once

// spread-overview workspace-border overlay node (constitution Principle II — render only).
// Strokes a border around each WORKSPACE CLUSTER region (the expo-style grid that
// separates the workspaces), plus optionally a highlight border around ONE cluster.
// The cluster regions come from the SAME layout() output that hit_test_cluster() uses
// for US2 drops (Principle I). The highlight is a parameter (set_highlight), NOT a
// separate code path — US2's drop-target highlight (T021) is just
// set_highlight(cluster_under_cursor). Added/removed exactly like the label nodes.

#include <wayfire/scene.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

#include <vector>
#include <string>

#include "layout.hpp"

namespace wf
{
namespace spread
{
class border_node_t : public wf::scene::node_t
{
    class render_instance_t : public wf::scene::simple_render_instance_t<border_node_t>
    {
      public:
        using simple_render_instance_t::simple_render_instance_t;

        void render(const wf::scene::render_instruction_t& data)
        {
            auto tex = self->tex.get_texture();
            if (tex)
            {
                data.pass->add_texture(tex, data.target, self->geometry, data.damage, self->alpha);
            }
        }
    };

  public:
    border_node_t() : node_t(false)
    {}

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *output) override
    {
        instances.push_back(std::make_unique<render_instance_t>(this, push_damage, output));
    }

    wf::geometry_t get_bounding_box() override
    {
        return geometry;
    }

    std::string stringify() const override
    {
        return "spread-overview-borders";
    }

    // Full content — called on build. `cluster_rects` are the per-workspace regions from
    // layout() (output-local), the same geometry hit_test_cluster() resolves drops to.
    // 002 (FR-011): the drop-target highlight moved to highlight_node_t below, so this node
    // is now purely the static grid — it re-renders only when the layout changes, never
    // while dragging.
    void set_content(wf::geometry_t output_geometry,
        std::vector<rectf> cluster_rects,
        int border_size, wf::color_t border_color);

    // Overlay opacity, driven by the exit fade (T027 A2). 1 = opaque, 0 = fully faded out.
    float alpha = 1.0f;

  private:
    void rerender();

    wf::geometry_t geometry{0, 0, 0, 0};
    std::vector<rectf> clusters;
    int border_size = 2;
    wf::color_t border_color{0.9, 0.9, 0.9, 1.0};
    wf::owned_texture_t tex;
};

// 002 (FR-011): the drop-target highlight, split out of border_node_t so it can FADE.
// It strokes one cluster with four solid rectangles via render_pass_t::add_rect — no
// texture at all, so the fade is just the colour's alpha and nothing is re-uploaded per
// frame. (In 001 the highlight was baked into border_node_t's single cairo texture
// together with the grid, which is exactly why it could only switch instantly.)
//
// Two slots — an outgoing and an incoming cluster — so moving the drag from one workspace
// to the next CROSS-fades instead of flicking: `progress` runs 0 -> 1, the incoming cell
// drawn at `progress` and the outgoing one at `1 - progress`. Sits above the grid in the
// OVERLAY layer; the plugin drives `progress` from a fixed ~120 ms clock.
class highlight_node_t : public wf::scene::node_t
{
    class render_instance_t : public wf::scene::simple_render_instance_t<highlight_node_t>
    {
      public:
        using simple_render_instance_t::simple_render_instance_t;

        void render(const wf::scene::render_instruction_t& data)
        {
            // Outgoing first, incoming on top: while they overlap (they never do, being
            // different cells) the incoming one should win.
            self->render_cell(data, self->prev_index, 1.0 - self->progress);
            self->render_cell(data, self->cur_index, self->progress);
        }
    };

  public:
    highlight_node_t() : node_t(false)
    {}

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *output) override
    {
        instances.push_back(std::make_unique<render_instance_t>(this, push_damage, output));
    }

    wf::geometry_t get_bounding_box() override
    {
        return geometry;
    }

    std::string stringify() const override
    {
        return "spread-overview-highlight";
    }

    // Same cluster regions as the border/dim/hit-test (Principle I).
    void set_content(wf::geometry_t output_geometry, std::vector<rectf> cluster_rects,
        int highlight_size, wf::color_t highlight_color);

    // Point the highlight at another cluster (-1 = none). Returns true when the target
    // actually changed, i.e. when the caller should (re)start the cross-fade clock.
    bool set_highlight(int cluster_index);

    // Cross-fade position, 0 -> 1. Written every frame by the plugin's animation tick.
    void set_progress(double p);

    // Overlay opacity, driven by the exit fade — same as the other overlay nodes.
    float alpha = 1.0f;

  private:
    void render_cell(const wf::scene::render_instruction_t& data, int index, double a);

    wf::geometry_t geometry{0, 0, 0, 0};
    std::vector<rectf> clusters;
    int size = 4;
    wf::color_t color{0.3, 0.6, 1.0, 1.0};
    int cur_index = -1, prev_index = -1;
    double progress = 1.0;
};

// Per-workspace dim veil (the "expo + scale" merge). Draws a translucent veil over the
// WHOLE output, then punches a fully-transparent hole over the ACTIVE cluster so it reads
// bright (expo's inactive_brightness focus cue). Sits at the BACK of the WORKSPACE layer —
// ABOVE the wallpaper/bottom panels, BELOW the view thumbnails — so windows stay crisp
// (scale's clarity) while their backdrops dim per workspace. Same cluster regions as
// border_node_t / hit_test_cluster() (Principle I). set_active() re-points the bright cell
// (current workspace when idle, drop-target while dragging) without a full rebuild.
class dim_node_t : public wf::scene::node_t
{
    class render_instance_t : public wf::scene::simple_render_instance_t<dim_node_t>
    {
      public:
        using simple_render_instance_t::simple_render_instance_t;

        void render(const wf::scene::render_instruction_t& data)
        {
            auto tex = self->tex.get_texture();
            if (tex)
            {
                data.pass->add_texture(tex, data.target, self->geometry, data.damage, self->alpha);
            }
        }
    };

  public:
    dim_node_t() : node_t(false)
    {}

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *output) override
    {
        instances.push_back(std::make_unique<render_instance_t>(this, push_damage, output));
    }

    wf::geometry_t get_bounding_box() override
    {
        return geometry;
    }

    std::string stringify() const override
    {
        return "spread-overview-dim";
    }

    // `veil` is the dim tint (RGB) + full-strength opacity (A, applied at brightness 0).
    // `inactive_brightness` follows expo's model: inactive cells sit at this brightness, so
    // their veil alpha = veil.a * (1 - inactive_brightness); the active cell is fully bright.
    void set_content(wf::geometry_t output_geometry,
        std::vector<rectf> cluster_rects,
        wf::color_t veil, double inactive_brightness, int active_index);

    // Re-point the bright (un-veiled) cluster. -1 = all cells dim. Re-renders on change.
    void set_active(int cluster_index);

    // Overlay opacity, driven by the exit fade (T027 A2). 1 = opaque, 0 = fully faded out.
    float alpha = 1.0f;

  private:
    void rerender();

    wf::geometry_t geometry{0, 0, 0, 0};
    std::vector<rectf> clusters;
    wf::color_t veil_color{0.0, 0.0, 0.0, 1.0};
    double inactive_brightness = 0.7;
    int active = -1;
    wf::owned_texture_t tex;
};

// Per-cell wallpaper (US-wallpaper / B2). Blits ONE already-loaded image (the desktop
// wallpaper, uploaded once in wallpaper.cpp — B1) scaled into EACH workspace cell, the
// expo-style tile backdrop. Sits at the very BACK of the WORKSPACE layer — BELOW the dim
// veil and the thumbnails (add_back after the veil) — so each cell reads as its own little
// desktop, dimmed by the veil unless it is the active cell. Same cluster regions as
// border/dim/hit-test (Principle I). This node does NOT own the texture (the plugin's
// wallpaper_tex does, for the plugin lifetime) — it only holds the shared texture_t handle
// and the destination cell rects. FAIL-SOFT: a null texture (load failed) draws NOTHING,
// so the overview falls back to the real desktop background showing through.
class wallpaper_node_t : public wf::scene::node_t
{
    class render_instance_t : public wf::scene::simple_render_instance_t<wallpaper_node_t>
    {
      public:
        using simple_render_instance_t::simple_render_instance_t;

        void render(const wf::scene::render_instruction_t& data)
        {
            // Fail-soft: no wallpaper loaded -> draw nothing (cells show the real desktop).
            if (!self->tex)
            {
                return;
            }

            // Blit the SAME cover-cropped source into every cell. add_texture scales the
            // texture's source box into each destination rect; the source box was set once
            // in set_content (cells are uniform), so all cells show an undistorted crop.
            for (const auto& cell : self->cells)
            {
                data.pass->add_texture(self->tex, data.target, cell, data.damage, self->alpha);
            }
        }
    };

  public:
    wallpaper_node_t() : node_t(false)
    {}

    void gen_render_instances(std::vector<wf::scene::render_instance_uptr>& instances,
        wf::scene::damage_callback push_damage, wf::output_t *output) override
    {
        instances.push_back(std::make_unique<render_instance_t>(this, push_damage, output));
    }

    wf::geometry_t get_bounding_box() override
    {
        return geometry;
    }

    std::string stringify() const override
    {
        return "spread-overview-wallpaper";
    }

    // `texture` is the shared loaded wallpaper (nullptr => feature disabled, draws nothing).
    // `tex_size` is its pixel size, used to compute the cover-crop source box. `cell_rects`
    // are the per-workspace destination regions (output-local), same as the veil/border.
    void set_content(wf::geometry_t output_geometry,
        std::vector<rectf> cell_rects,
        std::shared_ptr<wf::texture_t> texture, wf::dimensions_t tex_size);

    // Overlay opacity, driven by the exit fade (T027 A2). 1 = opaque, 0 = fully faded out.
    float alpha = 1.0f;

  private:
    wf::geometry_t geometry{0, 0, 0, 0};
    std::vector<wf::geometry_t> cells;
    std::shared_ptr<wf::texture_t> tex;
};
} // namespace spread
} // namespace wf
