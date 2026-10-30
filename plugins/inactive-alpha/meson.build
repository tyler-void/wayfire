# inactive-alpha — in-tree plugin (constitution Principle VIII).
# Inherits the parent build's resolved dependency objects; never re-declares wlroots.
# Lives in its own directory with one appended line in plugins/meson.build, so an
# upstream sync has nothing to conflict with (docs/SYNCING-UPSTREAM.md).

inactive_alpha_inc = [
    wayfire_api_inc,
    wayfire_conf_inc,
    plugins_common_inc,
    include_directories('.', 'src'),
]

inactive_alpha_deps = [
    wlroots, pixman, wfconfig, wftouch,
    json,
    plugin_pch_dep,
]

# policy.cpp is the PURE decision core (Principle I), unit-tested on its own in
# test/plugins/inactive-alpha-policy.cpp; plugin.cpp is the thin API contact.
inactive_alpha_src = [
    'src/plugin.cpp',
    'src/policy.cpp',
]

shared_module('inactive-alpha', inactive_alpha_src,
    include_directories: inactive_alpha_inc,
    dependencies: inactive_alpha_deps,
    install: true,
    install_dir: conf_data.get('PLUGIN_PATH'))
