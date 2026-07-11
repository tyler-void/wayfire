// spread-overview — plugin entry point.
// The per-output session lives in overview.{hpp,cpp}; this file only registers it.

#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>

#include "overview.hpp"

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wf::spread::spread_overview_t>);
