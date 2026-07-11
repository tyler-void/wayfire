// Unit tests for the pure spread-overview layout core (constitution Principle I).
// Built with -Dtests=true (doctest). Asserts the invariants in
// specs/001-spread-overview/contracts/layout.md. No compositor required.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../../plugins/spread-overview/src/layout.cpp"

using namespace wf::spread;

static bool overlaps(const rectf& a, const rectf& b)
{
    return (a.x < b.x + b.w) && (b.x < a.x + a.w) &&
           (a.y < b.y + b.h) && (b.y < a.y + a.h);
}

static layout_options opts()
{
    return layout_options{20.0, 40.0, 20.0, 1.0, 0.05};
}

TEST_CASE("invariant 1: cluster coverage == grid cells")
{
    auto r = layout({}, {3, 3}, {1920, 1080}, opts());
    CHECK(r.clusters.size() == 9);
    CHECK(r.views.empty());
}

TEST_CASE("invariant 9: cluster regions are independent of content density")
{
    std::vector<layout_input_view> dense_a, dense_b;
    for (uint32_t i = 0; i < 8; i++)
    {
        dense_a.push_back({i, {0, 0}, {800, 600}});     // 8 in A, 1 in B
        dense_b.push_back({i + 100, {2, 2}, {800, 600}}); // 8 in B (mirror)
    }
    dense_a.push_back({50, {2, 2}, {800, 600}});
    dense_b.push_back({150, {0, 0}, {800, 600}});

    auto ra = layout(dense_a, {3, 3}, {1920, 1080}, opts());
    auto rb = layout(dense_b, {3, 3}, {1920, 1080}, opts());

    REQUIRE(ra.clusters.size() == rb.clusters.size());
    for (size_t i = 0; i < ra.clusters.size(); i++)
    {
        CHECK(ra.clusters[i].region.x == doctest::Approx(rb.clusters[i].region.x));
        CHECK(ra.clusters[i].region.y == doctest::Approx(rb.clusters[i].region.y));
        CHECK(ra.clusters[i].region.w == doctest::Approx(rb.clusters[i].region.w));
        CHECK(ra.clusters[i].region.h == doctest::Approx(rb.clusters[i].region.h));
    }
}

TEST_CASE("invariant 3: zero overlap, including maximized views")
{
    std::vector<layout_input_view> views;
    // two 'maximized' views (natural size == output) on the same workspace
    views.push_back({1, {1, 1}, {1920, 1080}});
    views.push_back({2, {1, 1}, {1920, 1080}});
    // plus a spread across cells
    for (uint32_t i = 0; i < 9; i++)
    {
        views.push_back({10 + i, {(int)(i % 3), (int)(i / 3)}, {640, 480}});
    }
    auto r = layout(views, {3, 3}, {1920, 1080}, opts());
    REQUIRE(r.views.size() == views.size());
    for (size_t i = 0; i < r.views.size(); i++)
    {
        for (size_t j = i + 1; j < r.views.size(); j++)
        {
            CHECK_FALSE(overlaps(r.views[i].target_rect, r.views[j].target_rect));
        }
    }
}

TEST_CASE("invariant 6: hit-test round-trip on every thumbnail center")
{
    std::vector<layout_input_view> views;
    for (uint32_t i = 0; i < 9; i++)
    {
        views.push_back({i, {(int)(i % 3), (int)(i / 3)}, {700, 500}});
    }
    auto r = layout(views, {3, 3}, {1920, 1080}, opts());
    for (auto& v : r.views)
    {
        const double cx = v.target_rect.x + v.target_rect.w / 2.0;
        const double cy = v.target_rect.y + v.target_rect.h / 2.0;
        auto ws = hit_test_cluster(r, cx, cy);
        REQUIRE(ws.has_value());
        CHECK(ws->x == v.cluster_ws.x);
        CHECK(ws->y == v.cluster_ws.y);
    }
}

TEST_CASE("invariant 7: a point in the gap between clusters returns nullopt")
{
    auto r = layout({}, {3, 3}, {1920, 1080}, opts());
    // Between cluster (0,0) and (1,0): just past the first cell's right edge.
    const auto& c0 = r.clusters[0];
    const double gap_x = c0.region.x + c0.region.w + 1.0; // inside cluster_gap
    const double y = c0.region.y + c0.region.h / 2.0;
    CHECK_FALSE(hit_test_cluster(r, gap_x, y).has_value());
}

TEST_CASE("invariant 8/2: determinism + grouping")
{
    std::vector<layout_input_view> views = {
        {3, {2, 0}, {800, 600}}, {1, {0, 0}, {800, 600}}, {2, {1, 0}, {800, 600}}};
    auto r1 = layout(views, {3, 1}, {1920, 1080}, opts());
    auto r2 = layout(views, {3, 1}, {1920, 1080}, opts());
    REQUIRE(r1.views.size() == 3);
    CHECK(r1.views.size() == r2.views.size());
    for (auto& vo : r1.views)
    {
        // each view lands in the cluster matching its source workspace
        for (auto& in : views)
        {
            if (in.id == vo.id)
            {
                CHECK(vo.cluster_ws.x == in.source_ws.x);
                CHECK(vo.cluster_ws.y == in.source_ws.y);
            }
        }
    }
}

TEST_CASE("invariant 10: over-dense cluster still zero-overlap, flags over_dense")
{
    std::vector<layout_input_view> views;
    for (uint32_t i = 0; i < 60; i++) // pathologically dense single cluster
    {
        views.push_back({i, {0, 0}, {1600, 1000}});
    }
    auto r = layout(views, {1, 1}, {800, 600}, opts());
    // still no overlap
    for (size_t i = 0; i < r.views.size(); i++)
    {
        for (size_t j = i + 1; j < r.views.size(); j++)
        {
            CHECK_FALSE(overlaps(r.views[i].target_rect, r.views[j].target_rect));
        }
    }
    CHECK(r.clusters[0].over_dense);
}

TEST_CASE("edge-to-edge (cluster_gap=0, outer_margin=0): packing + hit-test stay meaningful")
{
    // spacing, cluster_gap=0, outer_margin=0, max_scale, min_scale
    layout_options o{20.0, 0.0, 0.0, 1.0, 0.05};

    std::vector<layout_input_view> views;
    for (uint32_t i = 0; i < 12; i++)
    {
        views.push_back({i, {(int)(i % 3), (int)(i / 3) % 3}, {700, 500}});
    }
    auto r = layout(views, {3, 3}, {1920, 1080}, o);

    // #9 density-invariance still holds at gap=0 (regions are a pure function of geometry)
    auto empty = layout({}, {3, 3}, {1920, 1080}, o);
    REQUIRE(r.clusters.size() == empty.clusters.size());
    for (size_t i = 0; i < r.clusters.size(); i++)
    {
        CHECK(r.clusters[i].region.x == doctest::Approx(empty.clusters[i].region.x));
        CHECK(r.clusters[i].region.w == doctest::Approx(empty.clusters[i].region.w));
    }

    // #3 zero overlap still holds with no gap between cells
    for (size_t i = 0; i < r.views.size(); i++)
    {
        for (size_t j = i + 1; j < r.views.size(); j++)
        {
            CHECK_FALSE(overlaps(r.views[i].target_rect, r.views[j].target_rect));
        }
    }

    // Clusters tile the whole output edge-to-edge -> every in-bounds point resolves to a
    // cluster (never nullopt). #7 ("gap -> nullopt") is not vacuous, it just means
    // "outside the output -> nullopt" when gap=0.
    CHECK(hit_test_cluster(r, 5, 5).has_value());                 // top-left cell
    CHECK(hit_test_cluster(r, 1915, 1075).has_value());           // bottom-right cell
    CHECK(hit_test_cluster(r, 640, 360).has_value());             // interior
    CHECK_FALSE(hit_test_cluster(r, 1920 + 10, 500).has_value()); // outside output -> nullopt

    // A boundary point resolves to exactly one cluster (half-open [x, x+w)).
    auto at_boundary = hit_test_cluster(r, 640, 360); // x=640 is the (0,0)|(1,0) edge
    REQUIRE(at_boundary.has_value());
    CHECK(at_boundary->x == 1); // belongs to the right cell, not the left
}
