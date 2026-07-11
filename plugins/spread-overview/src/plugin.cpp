// spread-overview — plugin entry point.
// This turn: minimal registering skeleton (task T004) that loads on master and
// proves the in-tree build + ABI. Lifecycle (activator + input grab), rendering,
// input and relocate arrive in tasks T009+ (see specs/001-spread-overview/tasks.md).

#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/util/log.hpp>

namespace wf
{
namespace spread
{
class spread_overview_t : public wf::per_output_plugin_instance_t
{
  public:
    void init() override
    {
        LOGI("spread-overview: initialized on output ", output->to_string());
    }

    void fini() override
    {
        LOGI("spread-overview: finalized on output ", output->to_string());
    }
};
} // namespace spread
} // namespace wf

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wf::spread::spread_overview_t>);
