// Unit tests for the pure spread-overview layout core (constitution Principle I).
// Built with -Dtests=true (doctest). Asserts the invariants in
// specs/001-spread-overview/contracts/layout.md. No compositor required.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../../plugins/spread-overview/src/layout.cpp"

#include <map>
#include <random>

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

// =================================================================================
// 002-spread-refine additions (contract: specs/002-spread-refine/contracts/layout.md §4)
// =================================================================================

// ---------------------------------------------------------------------------------
// GENERATED — do not edit by hand.
//   python3 specs/002-spread-refine/reference/layout_ref.py --golden /tmp/g.json
//   cmp /tmp/g.json specs/002-spread-refine/reference/golden-fixture.json
// then regenerate this block. The test embeds the numbers so it needs neither
// JSON nor Python at build or run time (contract §4.1).
// Fixture: 3440x1440 output, 3x3 grid, spacing 20, cluster_gap 0, outer_margin 0,
// max_scale 1, tolerance 0.5 px.
// ---------------------------------------------------------------------------------

static constexpr double GOLDEN_TOLERANCE_PX = 0.5;
static constexpr int GOLDEN_N = 18;

struct golden_view_t
{
    uint32_t id;
    int ws_x, ws_y;
    double w, h;   // natural_size
    double px, py; // natural_pos (workspace-local)
    const char *label;
};

static const golden_view_t GOLDEN_VIEWS[GOLDEN_N] = {
    { 0, 0, 0,   3440.0,   1440.0,      0.0,      0.0, "librewolf"},
    { 1, 0, 0,   1100.0,    700.0,   2200.0,    600.0, "kitty"},
    { 2, 0, 0,    620.0,    460.0,    300.0,    800.0, "pavucontrol"},
    { 3, 1, 0,   1720.0,   1440.0,      0.0,      0.0, "code"},
    { 4, 1, 0,   1720.0,   1440.0,   1720.0,      0.0, "librewolf"},
    { 5, 1, 0,    400.0,    560.0,   1500.0,    500.0, "calc"},
    { 6, 2, 0,   1720.0,   1440.0,      0.0,      0.0, "kitty"},
    { 7, 2, 0,   1100.0,    700.0,   1900.0,    100.0, "kitty"},
    { 8, 2, 0,    900.0,    560.0,   2300.0,    850.0, "kitty"},
    { 9, 2, 0,    700.0,    900.0,   1800.0,    500.0, "kitty"},
    {10, 0, 1,   3440.0,   1440.0,      0.0,      0.0, "librewolf"},
    {11, 1, 1,   1000.0,    700.0,    600.0,    300.0, "file dialog"},
    {12, 1, 1,    800.0,    800.0,   1900.0,    300.0, "imv"},
    {13, 0, 2,   1600.0,   1100.0,    200.0,    100.0, "obsidian"},
    {14, 0, 2,   1300.0,    800.0,   1900.0,    150.0, "kitty"},
    {15, 0, 2,    500.0,    350.0,   2000.0,   1000.0, "btop mini"},
    {16, 0, 2,    420.0,    220.0,    700.0,   1200.0, "notif"},
    {17, 2, 2,   1200.0,    700.0,   1100.0,    400.0, "mpv"},
};

// Expected target_rect per view id, from the reference oracle.
struct golden_rect_t { double x, y, w, h; };

static const golden_rect_t GOLDEN_001[GOLDEN_N] = {
    {20.0, 165.5813953488372, 355.5555555555556, 148.8372093023256},
    {516.4857881136952, 203.82428940568474, 113.6950904392765, 72.3514211886305},
    {916.8475452196383, 216.22739018087856, 64.0826873385013, 47.5452196382429},
    {1166.6666666666667, 91.16279069767441, 355.5555555555556, 297.6744186046512},
    {1542.2222222222224, 91.16279069767441, 355.5555555555556, 297.6744186046512},
    {2054.2118863049095, 182.1188630490956, 82.68733850129199, 115.7622739018088},
    {2365.694444444445, 20.0, 250.83333333333334, 210.0},
    {2786.4583333333335, 73.95833333333333, 160.41666666666669, 102.08333333333334},
    {3176.5972222222226, 84.16666666666666, 131.25, 81.66666666666667},
    {2440.069444444445, 289.375, 102.08333333333334, 131.25},
    {47.77777777777783, 500.0, 1051.111111111111, 440.00000000000006},
    {1166.6666666666667, 529.8333333333334, 543.3333333333334, 380.3333333333333},
    {1784.3333333333333, 502.66666666666663, 434.6666666666667, 434.6666666666667},
    {45.05050505050505, 980.0, 305.4545454545455, 210.0},
    {449.2424242424243, 1008.6363636363636, 248.1818181818182, 152.72727272727275},
    {901.1616161616163, 1051.590909090909, 95.45454545454545, 66.81818181818183},
    {157.6868686868687, 1294.0, 80.18181818181819, 42.0},
    {2489.5238095238096, 980.0, 754.2857142857142, 440.0},
}; // 001

static const golden_rect_t GOLDEN_002_B10[GOLDEN_N] = {
    {168.1653746770026, 91.16279069767441, 711.1111111111112, 297.6744186046512},
    {899.2764857881139, 167.6485788113695, 227.390180878553, 144.702842377261},
    {20.0, 192.45478036175712, 128.1653746770026, 95.0904392764858},
    {1166.6666666666667, 40.0, 477.77777777777777, 400.0},
    {1795.5555555555557, 40.0, 477.77777777777777, 400.0},
    {1664.4444444444446, 162.22222222222223, 111.11111111111111, 155.55555555555557},
    {2313.3333333333335, 69.5022624434389, 407.30015082956265, 340.9954751131222},
    {2926.395173453997, 157.11915535444945, 260.48265460030166, 165.76168929110108},
    {3206.8778280542983, 173.69532428355956, 213.12217194570138, 132.60935143288086},
    {2740.633484162896, 133.4389140271493, 165.76168929110108, 213.12217194570138},
    {47.77777777777794, 500.0, 1051.111111111111, 439.99999999999994},
    {1215.0, 527.5, 549.9999999999999, 384.99999999999994},
    {1785.0, 500.0, 439.99999999999994, 439.99999999999994},
    {155.0785340314136, 1049.301919720768, 438.39441535776615, 301.3961605584642},
    {770.4712041884817, 1090.4013961605585, 356.195462478185, 219.19720767888307},
    {613.4729493891798, 1152.0506108202444, 136.99825479930192, 95.89877835951134},
    {20.0, 1169.8603839441537, 115.07853403141361, 60.27923211169284},
    {2489.5238095238096, 980.0, 754.2857142857142, 440.0},
}; // 002_boost_1.0

static const golden_rect_t GOLDEN_002_B15[GOLDEN_N] = {
    {196.7795083443882, 104.15761804156637, 649.0247138014051, 271.68476391686727},
    {865.8042221457933, 156.99831310699489, 260.8624445208732, 166.00337378601023},
    {20.0, 181.83985980772695, 156.7795083443882, 116.3202803845461},
    {1166.6666666666667, 46.16937745372968, 463.03982052720136, 387.66124509254064},
    {1810.293512806132, 46.16937745372968, 463.03982052720136, 387.66124509254064},
    {1649.706487193868, 141.58908207141522, 140.58702561226397, 196.82183585716956},
    {2313.3333333333335, 92.49238799478033, 352.3792953458026, 295.01522401043934},
    {2876.012215455996, 149.8706865303755, 283.26355661881985, 180.25862693924898},
    {3179.275772074816, 165.10801797883136, 240.72422792518492, 149.7839640423373},
    {2685.7126286791363, 130.52169421487602, 170.29958677685954, 218.95661157024796},
    {47.77777777777794, 500.0, 1051.111111111111, 439.99999999999994},
    {1207.1875, 522.03125, 565.6249999999999, 395.93749999999994},
    {1792.8125, 500.0, 440.00000000000006, 440.00000000000006},
    {174.1540751767771, 1062.0285324401439, 401.37154199230866, 275.9429351197122},
    {770.168961667287, 1090.3083984617292, 356.4977049993797, 219.38320307654138},
    {595.5256171690858, 1145.8748294256295, 154.64334449820123, 108.25034114874086},
    {20.0, 1164.8644088822725, 134.1540751767771, 70.27118223545466},
    {2489.5238095238096, 980.0, 754.2857142857142, 440.0},
}; // 002_boost_1.5

static const golden_rect_t GOLDEN_002_B25[GOLDEN_N] = {
    {241.24649684192914, 124.35186347094994, 552.5410967499058, 231.29627305810013},
    {813.7875935918349, 140.44756765800813, 312.87907307483164, 199.10486468398375},
    {20.0, 165.34404149412308, 201.24649684192914, 149.31191701175388},
    {1166.6666666666667, 57.43264659270997, 436.13312202852615, 365.13470681458006},
    {1837.2002113048075, 57.43264659270997, 436.13312202852615, 365.13470681458006},
    {1622.799788695193, 103.91970417326993, 194.40042260961437, 272.16059165346013},
    {2313.3333333333335, 123.82322713257966, 277.5334018499486, 232.35354573484068},
    {2807.350548132922, 139.99250599520383, 314.30926687221654, 200.01498800959234},
    {3141.6598150051386, 153.4052757793765, 278.3401849948612, 173.18944844124698},
    {2610.866735183282, 126.54612024665983, 176.48381294964028, 226.90775950668035},
    {47.77777777777794, 500.0, 1051.111111111111, 439.99999999999994},
    {1197.8125, 515.46875, 584.3750000000001, 409.06250000000006},
    {1802.1875, 500.0, 439.99999999999994, 439.99999999999994},
    {403.67503075030754, 1198.3763837638376, 322.36162361623616, 221.62361623616235},
    {412.1525215252153, 980.0, 322.3616236162361, 198.37638376383762},
    {746.0366543665436, 1308.80073800738, 158.85608856088558, 111.19926199261991},
    {241.77392373923743, 1345.670848708487, 141.9011070110701, 74.32915129151291},
    {2489.5238095238096, 980.0, 754.2857142857142, 440.0},
}; // 002_boost_2.5

static const golden_rect_t *const GOLDEN_002[3] = {
    GOLDEN_002_B10, GOLDEN_002_B15, GOLDEN_002_B25,
};
static const double GOLDEN_BETAS[3] = {1.0, 1.5, 2.5};

// ---------------------------------------------------------------------------------
// Helpers for the 002 tests. All geometric comparisons here use an epsilon: at
// spacing 0 adjacent thumbnails touch exactly, and the strict overlaps() above would
// report ~1e-12 px false positives. The existing (spacing-20) tests keep using it.
// ---------------------------------------------------------------------------------
static constexpr double EPS = 1e-6;

static bool overlaps_eps(const rectf& a, const rectf& b)
{
    return (a.x < b.x + b.w - EPS) && (b.x < a.x + a.w - EPS) &&
           (a.y < b.y + b.h - EPS) && (b.y < a.y + a.h - EPS);
}

static bool inside_eps(const rectf& r, const rectf& region)
{
    return (r.x >= region.x - EPS) && (r.y >= region.y - EPS) &&
           (r.x + r.w <= region.x + region.w + EPS) &&
           (r.y + r.h <= region.y + region.h + EPS);
}

// Two thumbnails are in the same packing row iff their rects overlap vertically:
// a row is bottom-aligned, and rows are stacked with >= 0 spacing, so distinct rows
// never overlap (they only touch at spacing 0).
static bool same_row(const rectf& a, const rectf& b)
{
    return (a.y < b.y + b.h - EPS) && (b.y < a.y + a.h - EPS);
}

static layout_options opts_002(double spacing, double cluster_gap, double outer_margin, double beta)
{
    layout_options o;
    o.spacing = spacing;
    o.cluster_gap = cluster_gap;
    o.outer_margin = outer_margin;
    o.max_scale = 1.0;
    o.min_scale = 0.05;
    o.small_window_boost = beta;
    return o;
}

// The golden fixture as layout() inputs.
static std::vector<layout_input_view> golden_inputs()
{
    std::vector<layout_input_view> v;
    for (int i = 0; i < GOLDEN_N; i++)
    {
        const auto& g = GOLDEN_VIEWS[i];
        v.push_back(layout_input_view{g.id, ivec2{g.ws_x, g.ws_y},
            dimf{g.w, g.h}, pointf{g.px, g.py}});
    }

    return v;
}

static std::map<uint32_t, rectf> by_id(const layout_result& r)
{
    std::map<uint32_t, rectf> m;
    for (auto& v : r.views)
    {
        m[v.id] = v.target_rect;
    }

    return m;
}

TEST_CASE("invariant 13 (SC-004): golden parity with the reference oracle, 0.5 px")
{
    for (int bi = 0; bi < 3; bi++)
    {
        auto r = layout(golden_inputs(), {3, 3}, {3440, 1440},
            opts_002(20.0, 0.0, 0.0, GOLDEN_BETAS[bi]));
        REQUIRE(r.views.size() == (size_t)GOLDEN_N);

        auto got = by_id(r);
        for (int i = 0; i < GOLDEN_N; i++)
        {
            const auto& exp = GOLDEN_002[bi][i];
            const auto& g   = GOLDEN_VIEWS[i];
            REQUIRE(got.count(g.id) == 1);
            const auto& a = got[g.id];

            INFO("beta=" << GOLDEN_BETAS[bi] << " view " << g.id << " (" << g.label << ")");
            CHECK(std::abs(a.x - exp.x) <= GOLDEN_TOLERANCE_PX);
            CHECK(std::abs(a.y - exp.y) <= GOLDEN_TOLERANCE_PX);
            CHECK(std::abs(a.w - exp.w) <= GOLDEN_TOLERANCE_PX);
            CHECK(std::abs(a.h - exp.h) <= GOLDEN_TOLERANCE_PX);
        }
    }
}

// Test-only copy of 001's pack_cluster sizing — the "001 size" oracle for SC-002.
// Deliberately NOT named pack_cluster: this TU #includes layout.cpp, where that name
// now belongs to the 002 packer. Verified against the fixture's recorded 001 rects
// by the first subcase below, so it cannot drift from the algorithm it stands for.
static std::map<uint32_t, rectf> pack_cluster_001_oracle(const rectf& region,
    const std::vector<const layout_input_view*>& members, double spacing, double max_scale)
{
    std::map<uint32_t, rectf> out;
    const int n = (int)members.size();
    if (n == 0)
    {
        return out;
    }

    const double aspect = region.w / std::max(1.0, region.h);
    int cols = (int)std::round(std::sqrt((double)n * aspect));
    cols = std::max(1, std::min(cols, n));
    const int rows = (n + cols - 1) / cols;

    const double slot_w = std::max((region.w - (cols + 1) * spacing) / cols, 1.0);
    const double slot_h = std::max((region.h - (rows + 1) * spacing) / rows, 1.0);

    double scale = max_scale;
    for (auto *v : members)
    {
        scale = std::min(scale, std::min(slot_w / std::max(1.0, v->natural_size.w),
            slot_h / std::max(1.0, v->natural_size.h)));
    }

    std::vector<const layout_input_view*> ordered = members;
    std::sort(ordered.begin(), ordered.end(),
        [] (const layout_input_view *a, const layout_input_view *b) { return a->id < b->id; });

    for (int i = 0; i < n; i++)
    {
        const int r = i / cols, c = i % cols;
        const double sx = region.x + spacing + c * (slot_w + spacing);
        const double sy = region.y + spacing + r * (slot_h + spacing);
        const double tw = ordered[i]->natural_size.w * scale;
        const double th = ordered[i]->natural_size.h * scale;
        out[ordered[i]->id] = rectf{sx + (slot_w - tw) / 2.0, sy + (slot_h - th) / 2.0, tw, th};
    }

    return out;
}

static std::map<uint32_t, rectf> fixture_001_sizes()
{
    auto views = golden_inputs();
    auto o = opts_002(20.0, 0.0, 0.0, 1.5);
    std::map<uint32_t, rectf> all;
    for (int cy = 0; cy < 3; cy++)
    {
        for (int cx = 0; cx < 3; cx++)
        {
            std::vector<const layout_input_view*> members;
            for (auto& v : views)
            {
                if ((v.source_ws.x == cx) && (v.source_ws.y == cy))
                {
                    members.push_back(&v);
                }
            }

            auto reg = cluster_region(ivec2{cx, cy}, {3, 3}, {3440, 1440}, o);
            for (auto& kv : pack_cluster_001_oracle(reg, members, o.spacing, o.max_scale))
            {
                all[kv.first] = kv.second;
            }
        }
    }

    return all;
}

TEST_CASE("SC-002: no fixture thumbnail is smaller than under 001, at beta 1.0/1.5/2.5")
{
    auto old = fixture_001_sizes();

    SUBCASE("the 001 oracle reproduces the fixture's recorded 001 rects")
    {
        for (int i = 0; i < GOLDEN_N; i++)
        {
            const auto& g = GOLDEN_VIEWS[i];
            const auto& e = GOLDEN_001[i];
            REQUIRE(old.count(g.id) == 1);
            INFO("view " << g.id << " (" << g.label << ")");
            CHECK(std::abs(old[g.id].w - e.w) <= GOLDEN_TOLERANCE_PX);
            CHECK(std::abs(old[g.id].h - e.h) <= GOLDEN_TOLERANCE_PX);
        }
    }

    SUBCASE("002 never shrinks a fixture thumbnail")
    {
        for (int bi = 0; bi < 3; bi++)
        {
            auto got = by_id(layout(golden_inputs(), {3, 3}, {3440, 1440},
                opts_002(20.0, 0.0, 0.0, GOLDEN_BETAS[bi])));
            for (int i = 0; i < GOLDEN_N; i++)
            {
                const auto& g = GOLDEN_VIEWS[i];
                INFO("beta=" << GOLDEN_BETAS[bi] << " view " << g.id << " (" << g.label << ")");
                CHECK(got[g.id].w >= old[g.id].w - GOLDEN_TOLERANCE_PX);
                CHECK(got[g.id].h >= old[g.id].h - GOLDEN_TOLERANCE_PX);
            }
        }
    }

    SUBCASE("SC-001: the smallest thumbnail grows by at least 3x in area")
    {
        auto got = by_id(layout(golden_inputs(), {3, 3}, {3440, 1440},
            opts_002(20.0, 0.0, 0.0, 1.5)));
        double min_old = 1e18, min_new = 1e18;
        for (int i = 0; i < GOLDEN_N; i++)
        {
            const auto id = GOLDEN_VIEWS[i].id;
            min_old = std::min(min_old, old[id].w * old[id].h);
            min_new = std::min(min_new, got[id].w * got[id].h);
        }

        CHECK(min_new >= 3.0 * min_old);
        // the volume mixer (id 2) must be at least 128x96
        CHECK(got[2].w >= 128.0);
        CHECK(got[2].h >= 96.0);
    }
}

TEST_CASE("D4: dense clusters keep positive, non-overlapping, in-region thumbnails")
{
    // The three reproductions from layout_ref.py::dense_repros(): a 480x540 region
    // (4x2 grid on 1920x1080) at spacing 40, where GNOME's rule alone yields 0x0 or
    // negative widths, and s = 0 for the 85-window case.
    auto make_case = [] (int n_small, bool big, int n_uniform)
    {
        std::vector<layout_input_view> v;
        if (big)
        {
            v.push_back(layout_input_view{0, {0, 0}, {1920, 1080}, {0, 0}});
            for (int i = 1; i <= n_small; i++)
            {
                v.push_back(layout_input_view{(uint32_t)i, {0, 0}, {60, 60},
                    {(double)i * 10.0, 500.0}});
            }
        } else
        {
            for (int i = 0; i < n_uniform; i++)
            {
                v.push_back(layout_input_view{(uint32_t)i, {0, 0}, {800, 600}, {0, 0}});
            }
        }

        return v;
    };

    std::vector<std::vector<layout_input_view>> cases = {
        make_case(11, true, 0), make_case(12, true, 0), make_case(0, false, 85)};

    for (size_t c = 0; c < cases.size(); c++)
    {
        for (int bi = 0; bi < 3; bi++)
        {
            INFO("dense repro " << c << " beta=" << GOLDEN_BETAS[bi]);
            auto r = layout(cases[c], {4, 2}, {1920, 1080},
                opts_002(40.0, 0.0, 0.0, GOLDEN_BETAS[bi]));
            REQUIRE(r.views.size() == cases[c].size());

            const rectf region = r.clusters[0].region;
            for (size_t i = 0; i < r.views.size(); i++)
            {
                INFO("view " << r.views[i].id);
                CHECK(r.views[i].target_rect.w > 0.0);
                CHECK(r.views[i].target_rect.h > 0.0);
                CHECK(inside_eps(r.views[i].target_rect, region));
                for (size_t j = i + 1; j < r.views.size(); j++)
                {
                    CHECK_FALSE(overlaps_eps(r.views[i].target_rect, r.views[j].target_rect));
                }
            }
        }
    }
}

TEST_CASE("invariant 12: with small_window_boost = 1.0 a row has one uniform scale")
{
    std::mt19937 rng{12};
    std::uniform_int_distribution<int> nd{2, 12}, wd{80, 3000}, hd{60, 1400};

    for (int trial = 0; trial < 200; trial++)
    {
        const int n = nd(rng);
        std::vector<layout_input_view> views;
        for (int i = 0; i < n; i++)
        {
            const double w = wd(rng), h = hd(rng);
            std::uniform_int_distribution<int> xd{0, (int)(3440 - w)}, yd{0, (int)(1440 - h)};
            views.push_back(layout_input_view{(uint32_t)i, {0, 0}, {w, h},
                {(double)xd(rng), (double)yd(rng)}});
        }

        auto r = layout(views, {1, 1}, {3440, 1440}, opts_002(20.0, 0.0, 0.0, 1.0));
        auto got = by_id(r);

        for (int i = 0; i < n; i++)
        {
            for (int j = i + 1; j < n; j++)
            {
                if (!same_row(got[i], got[j]))
                {
                    continue;
                }

                const double si = got[i].w / views[i].natural_size.w;
                const double sj = got[j].w / views[j].natural_size.w;
                // only compare windows that are not pinned by the max_scale cap
                if ((si < 1.0 - 1e-9) && (sj < 1.0 - 1e-9))
                {
                    INFO("trial " << trial << " views " << i << "," << j);
                    CHECK(std::abs(si - sj) <= 1e-9);
                }
            }
        }
    }
}

TEST_CASE("randomized properties: overlap, bounds, cap, positivity, spatial order")
{
    std::mt19937 rng{11};
    const std::vector<ivec2> grids = {{1, 1}, {2, 2}, {3, 3}, {4, 2}};
    const std::vector<dimf> outputs = {{3440, 1440}, {1920, 1080}, {2560, 1600}};
    const std::vector<double> spacings = {0.0, 8.0, 20.0, 40.0};

    int clusters_checked = 0;
    for (int trial = 0; trial < 700; trial++)
    {
        const ivec2 grid = grids[rng() % grids.size()];
        const dimf out   = outputs[rng() % outputs.size()];
        const double sp  = spacings[rng() % spacings.size()];
        const double gap = (rng() % 2) ? 40.0 : 0.0;

        const bool dense = (rng() % 2) == 0;
        std::uniform_int_distribution<int> nd{1, dense ? 14 : 100};
        const int n = nd(rng);

        std::vector<layout_input_view> views;
        for (int i = 0; i < n; i++)
        {
            std::uniform_int_distribution<int> wd{1, (int)out.w}, hd{1, (int)out.h};
            const double w = wd(rng), h = hd(rng);
            std::uniform_int_distribution<int> xd{0, (int)(out.w - w)}, yd{0, (int)(out.h - h)};
            views.push_back(layout_input_view{(uint32_t)i, {0, 0}, {w, h},
                {(double)xd(rng), (double)yd(rng)}});
        }

        for (int bi = 0; bi < 3; bi++)
        {
            INFO("trial " << trial << " n=" << n << " spacing=" << sp
                          << " beta=" << GOLDEN_BETAS[bi]);
            auto r = layout(views, grid, out, opts_002(sp, gap, 0.0, GOLDEN_BETAS[bi]));
            REQUIRE(r.views.size() == views.size());
            clusters_checked++;

            const rectf region = r.clusters[0].region;
            for (size_t i = 0; i < r.views.size(); i++)
            {
                const auto& a = r.views[i];
                INFO("view " << a.id);
                // invariant 10: strictly positive
                CHECK(a.target_rect.w > 0.0);
                CHECK(a.target_rect.h > 0.0);
                // invariant 2: grouped in its own cluster's region
                CHECK(a.cluster_ws.x == 0);
                CHECK(a.cluster_ws.y == 0);
                CHECK(inside_eps(a.target_rect, region));
                // invariant 5: never upscaled past the cap
                CHECK(a.target_rect.w <= views[a.id].natural_size.w * 1.0 + EPS);
                CHECK(a.target_rect.h <= views[a.id].natural_size.h * 1.0 + EPS);

                for (size_t j = i + 1; j < r.views.size(); j++)
                {
                    const auto& b = r.views[j];
                    // invariant 3: zero overlap
                    CHECK_FALSE(overlaps_eps(a.target_rect, b.target_rect));

                    // invariant 11: spatial order
                    const auto& va = views[a.id];
                    const auto& vb = views[b.id];
                    const double cya = va.natural_pos.y + va.natural_size.h / 2.0;
                    const double cyb = vb.natural_pos.y + vb.natural_size.h / 2.0;
                    const double cxa = va.natural_pos.x + va.natural_size.w / 2.0;
                    const double cxb = vb.natural_pos.x + vb.natural_size.w / 2.0;
                    const bool a_before_b = (cya != cyb) ? (cya < cyb) : (a.id < b.id);
                    const double sa = a.target_rect.w / va.natural_size.w;
                    const double sb = b.target_rect.w / vb.natural_size.w;

                    if (same_row(a.target_rect, b.target_rect))
                    {
                        // within a row, x follows (cx, id)
                        const bool a_left = (cxa != cxb) ? (cxa < cxb) : (a.id < b.id);
                        CHECK((a.target_rect.x < b.target_rect.x) == a_left);
                    } else if ((sa < 1.0 - 1e-9) && (sb < 1.0 - 1e-9))
                    {
                        // Vertically disjoint AND neither is capped => genuinely different
                        // rows: within a row an uncapped thumbnail is bottom-aligned, so
                        // same-row uncapped rects always overlap vertically. (A CAPPED
                        // thumbnail is drawn at row_bottom - uncapped_height, i.e. it floats
                        // up inside its own row band, and two capped same-row windows can be
                        // disjoint - verified against layout_ref.py for grid 1x1, 3440x1440,
                        // spacing 8, beta 2.5, windows 1958x823@587,302 / 10x1242@2797,171 /
                        // 72x233@710,266 / 720x1039@900,255 / 454x61@1247,123, where ids 2
                        // and 4 share row 0. The C++ reproduces those rects exactly.)
                        // Across real rows, the upper row holds the earlier (cy, id) windows.
                        const bool a_above = a.target_rect.y < b.target_rect.y;
                        INFO("a: nat " << va.natural_size.w << "x" << va.natural_size.h
                                       << " pos " << va.natural_pos.x << "," << va.natural_pos.y
                                       << " rect " << a.target_rect.x << "," << a.target_rect.y
                                       << " " << a.target_rect.w << "x" << a.target_rect.h
                                       << " scale " << (a.target_rect.w / va.natural_size.w));
                        INFO("b: nat " << vb.natural_size.w << "x" << vb.natural_size.h
                                       << " pos " << vb.natural_pos.x << "," << vb.natural_pos.y
                                       << " rect " << b.target_rect.x << "," << b.target_rect.y
                                       << " " << b.target_rect.w << "x" << b.target_rect.h
                                       << " scale " << (b.target_rect.w / vb.natural_size.w));
                        CHECK(a_above == a_before_b);
                    }
                }
            }
        }
    }

    MESSAGE("randomized clusters checked: " << clusters_checked);
    CHECK(clusters_checked >= 2000);
}
