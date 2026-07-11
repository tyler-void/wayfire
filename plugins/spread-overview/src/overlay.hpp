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
                data.pass->add_texture(tex, data.target, self->geometry, data.damage);
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
    void set_content(wf::geometry_t output_geometry,
        std::vector<rectf> cluster_rects,
        int border_size, wf::color_t border_color,
        int highlight_size, wf::color_t highlight_color);

    // US2 hook (dormant this increment): highlight one cluster region (index into
    // cluster_rects), -1 = none. Re-renders + damages.
    void set_highlight(int cluster_index);

  private:
    void rerender();

    wf::geometry_t geometry{0, 0, 0, 0};
    std::vector<rectf> clusters;
    int border_size = 2, highlight_size = 4;
    wf::color_t border_color{0.9, 0.9, 0.9, 1.0};
    wf::color_t highlight_color{0.3, 0.6, 1.0, 1.0};
    int highlighted = -1;
    wf::owned_texture_t tex;
};
} // namespace spread
} // namespace wf
