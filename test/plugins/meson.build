subdir('common')
subdir('command')
subdir('vswitch')

# Pure layout core for spread-overview (constitution Principle I). No libwayfire
# link — the test #includes the pure layout.cpp directly; only doctest is needed.
spread_overview_layout_test = executable(
    'spread_overview_layout_test',
    'spread-overview-layout.cpp',
    dependencies: doctest,
    install: false)
test('Spread overview layout test', spread_overview_layout_test)

# Pure decision core for inactive-alpha (constitution Principle I). Same shape: the test
# #includes the pure policy.cpp directly, so only doctest is needed — no libwayfire link.
inactive_alpha_policy_test = executable(
    'inactive_alpha_policy_test',
    'inactive-alpha-policy.cpp',
    dependencies: doctest,
    install: false)
test('Inactive alpha policy test', inactive_alpha_policy_test)
