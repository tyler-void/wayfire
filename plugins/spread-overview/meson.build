# spread-overview — in-tree plugin (constitution Principle VIII).
# Inherits the parent build's resolved dependency objects; never re-declares wlroots.
# Uses only globally-available meson vars (no scale-specific includes).

spread_overview_inc = [
    wayfire_api_inc,
    wayfire_conf_inc,
    plugins_common_inc,
    include_directories('.', 'src'),
]

spread_overview_deps = [
    wlroots, pixman, wfconfig, wftouch,
    cairo, pango, pangocairo, json,
    plugin_pch_dep,
]

# Source list grows as tasks land. Now: entry + session lifecycle + pure layout.
# Later: render.cpp, input.cpp, move.cpp (tasks T011+).
spread_overview_src = [
    'src/plugin.cpp',
    'src/overview.cpp',
    'src/render.cpp',
    'src/input.cpp',
    'src/move.cpp',
    'src/overlay.cpp',
    'src/wallpaper.cpp',
    'src/layout.cpp',
]

shared_module('spread-overview', spread_overview_src,
    include_directories: spread_overview_inc,
    dependencies: spread_overview_deps,
    install: true,
    install_dir: conf_data.get('PLUGIN_PATH'))
