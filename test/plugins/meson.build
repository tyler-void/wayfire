subdir('command')

# Pure layout core for spread-overview (constitution Principle I). No libwayfire
# link — the test #includes the pure layout.cpp directly; only doctest is needed.
spread_overview_layout_test = executable(
    'spread_overview_layout_test',
    'spread-overview-layout.cpp',
    dependencies: doctest,
    install: false)
test('Spread overview layout test', spread_overview_layout_test)
