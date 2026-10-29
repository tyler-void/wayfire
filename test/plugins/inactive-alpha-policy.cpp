// Unit tests for the pure inactive-alpha policy (constitution Principle I).
// Built with -Dtests=enabled (doctest). Asserts the invariants in
// specs/004-inactive-alpha/contracts/policy.md. No compositor required.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../../plugins/inactive-alpha/src/policy.cpp"

#include <vector>

using namespace wf::inactive_alpha;

static constexpr uint32_t FOCUSED_ID = 42;
static constexpr uint32_t OTHER_ID   = 7;

static policy_input input(uint32_t focused, bool suppressed, double dim)
{
    policy_input in;
    in.focused    = focused;
    in.suppressed = suppressed;
    in.dim = dim;
    return in;
}

TEST_CASE("P1: the focused window is never dimmed")
{
    for (bool suppressed : {false, true})
    {
        for (double dim : {0.0, 0.5, 0.85, 1.0})
        {
            auto in = input(FOCUSED_ID, suppressed, dim);
            INFO("suppressed=" << suppressed << " dim=" << dim);
            CHECK(target_alpha(in, FOCUSED_ID) == doctest::Approx(1.0));
        }
    }
}

TEST_CASE("P2: suppression beats everything — this is the bug the daemon had")
{
    // The retired script decided by plugin NAME, so a plugin missing from its list left
    // windows dimmed inside an overview. Here suppression is unconditional.
    for (double dim : {0.0, 0.5, 0.85, 1.0})
    {
        auto in = input(FOCUSED_ID, true, dim);
        for (uint32_t id : {FOCUSED_ID, OTHER_ID, (uint32_t)0, (uint32_t)99999})
        {
            INFO("dim=" << dim << " id=" << id);
            CHECK(target_alpha(in, id) == doctest::Approx(1.0));
        }
    }
}

TEST_CASE("P3: others get exactly the configured value, clamped")
{
    CHECK(target_alpha(input(FOCUSED_ID, false, 0.85), OTHER_ID) == doctest::Approx(0.85));
    CHECK(target_alpha(input(FOCUSED_ID, false, 0.0), OTHER_ID) == doctest::Approx(0.0));

    // dim = 1.0 means "no dimming": it must yield exactly 1.0, not something near it.
    CHECK(target_alpha(input(FOCUSED_ID, false, 1.0), OTHER_ID) == doctest::Approx(1.0));

    // out of range is clamped, never extrapolated
    CHECK(target_alpha(input(FOCUSED_ID, false, -1.0), OTHER_ID) == doctest::Approx(0.0));
    CHECK(target_alpha(input(FOCUSED_ID, false, 2.0), OTHER_ID) == doctest::Approx(1.0));
}

TEST_CASE("P4: total — defined when nothing holds focus")
{
    // focused == 0 means "no window known to hold focus". Every window then dims, which
    // is precisely why the CALLER must retain the last focused id instead of zeroing it
    // when keyboard focus moves to a panel (FR-008). Documented here so the caller rule
    // is visible from the test, not only from the contract.
    auto in = input(0, false, 0.85);
    CHECK(target_alpha(in, OTHER_ID) == doctest::Approx(0.85));
    CHECK(target_alpha(in, FOCUSED_ID) == doctest::Approx(0.85));

    // id 0 with focused 0 is still well defined (it matches, so it reads as focused).
    CHECK(target_alpha(in, 0) == doctest::Approx(1.0));
}

TEST_CASE("P5: monotonic in dim for a non-focused window")
{
    double prev = -1.0;
    for (int i = 0; i <= 100; i++)
    {
        const double dim = i / 100.0;
        const double got = target_alpha(input(FOCUSED_ID, false, dim), OTHER_ID);
        INFO("dim=" << dim);
        CHECK(got >= prev);
        CHECK(got == doctest::Approx(dim));
        prev = got;
    }
}

TEST_CASE("the whole truth table: {suppressed} x {focused, other} x dim values")
{
    struct row { bool suppressed; uint32_t id; double dim; double expect; };

    const std::vector<row> table = {
        // suppressed -> always 1.0
        {true,  FOCUSED_ID, 0.00, 1.0}, {true,  OTHER_ID, 0.00, 1.0},
        {true,  FOCUSED_ID, 0.50, 1.0}, {true,  OTHER_ID, 0.50, 1.0},
        {true,  FOCUSED_ID, 0.85, 1.0}, {true,  OTHER_ID, 0.85, 1.0},
        {true,  FOCUSED_ID, 1.00, 1.0}, {true,  OTHER_ID, 1.00, 1.0},
        // not suppressed -> focused opaque, other dimmed at the configured value
        {false, FOCUSED_ID, 0.00, 1.0}, {false, OTHER_ID, 0.00, 0.00},
        {false, FOCUSED_ID, 0.50, 1.0}, {false, OTHER_ID, 0.50, 0.50},
        {false, FOCUSED_ID, 0.85, 1.0}, {false, OTHER_ID, 0.85, 0.85},
        {false, FOCUSED_ID, 1.00, 1.0}, {false, OTHER_ID, 1.00, 1.00},
    };

    for (const auto& r : table)
    {
        INFO("suppressed=" << r.suppressed << " id=" << r.id << " dim=" << r.dim);
        CHECK(target_alpha(input(FOCUSED_ID, r.suppressed, r.dim), r.id) ==
            doctest::Approx(r.expect));
    }
}

TEST_CASE("P4: purity — identical inputs give identical output")
{
    auto in = input(FOCUSED_ID, false, 0.85);
    const double first = target_alpha(in, OTHER_ID);
    for (int i = 0; i < 1000; i++)
    {
        CHECK(target_alpha(in, OTHER_ID) == first);
    }
}
