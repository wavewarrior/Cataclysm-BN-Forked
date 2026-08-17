// Unit tests for procedural normal-atlas generation (src/lighting/normal_gen.cpp).
//
// Tagged `[normal_gen]` only -- deliberately NOT `[.gpu]`. Every surface here is built
// in-test, so this needs no GPU, no tileset files, no loaded world and no SDL_Init, and
// must run in the ordinary `cata_test-tiles` CI lane.
//
// Each destination surface is prefilled with a LOUD sentinel rather than the neutral
// texel. Prefilling the neutral would make every "output is flat" assertion vacuous:
// it would pass just as well if the generator wrote nothing at all.

#include "catch/catch_amalgamated.hpp"
#include "lighting/normal_gen.h"

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <vector>

namespace {

using rgba = std::array<std::uint8_t, 4>;

/// The encoded neutral texel: flat normal, zero blend weight, opaque.
constexpr rgba NEUTRAL{128, 128, 0, 255};
/// Nothing the generator can legitimately produce, so its survival proves a miss.
constexpr rgba SENTINEL{7, 11, 13, 17};

/// An owned RGBA32 surface for the duration of one test.
class test_surface {
public:
    test_surface(int w, int h): s_(SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32)) {}
    ~test_surface() {
        if (s_ != nullptr) { SDL_DestroySurface(s_); }
    }
    test_surface(const test_surface&) = delete;
    test_surface(test_surface&&) = delete;
    auto operator=(const test_surface&) -> test_surface& = delete; // *NOPAD*
    auto operator=(test_surface&&) -> test_surface& = delete;      // *NOPAD*

    auto get() const -> SDL_Surface* { return s_; } // *NOPAD*
    auto width() const -> int { return s_->w; }
    auto height() const -> int { return s_->h; }

    auto fill(const rgba& c) -> void {
        for (int y = 0; y < s_->h; ++y) {
            for (int x = 0; x < s_->w; ++x) { set(x, y, c); }
        }
    }
    /// Paints every pixel from `f( x, y ) -> rgba`.
    template <typename F> auto paint(F&& f) -> void {
        for (int y = 0; y < s_->h; ++y) {
            for (int x = 0; x < s_->w; ++x) { set(x, y, f(x, y)); }
        }
    }
    auto set(int x, int y, const rgba& c) -> void {
        std::uint8_t* px = byte_at(x, y);
        px[0] = c[0];
        px[1] = c[1];
        px[2] = c[2];
        px[3] = c[3];
    }
    auto at(int x, int y) const -> rgba {
        const std::uint8_t* px = byte_at(x, y);
        return rgba{px[0], px[1], px[2], px[3]};
    }

private:
    auto byte_at(int x, int y) const -> std::uint8_t* { // *NOPAD*
        return static_cast<std::uint8_t*>(s_->pixels) + static_cast<std::ptrdiff_t>(y) * s_->pitch
             + static_cast<std::ptrdiff_t>(x) * 4;
    }

    SDL_Surface* s_ = nullptr;
};

/// Counts texels whose R or G departs from neutral by more than `tol` -- "is there
/// actually relief here".
auto relief_texels(const test_surface& s, int tol) -> int {
    int n = 0;
    for (int y = 0; y < s.height(); ++y) {
        for (int x = 0; x < s.width(); ++x) {
            const auto c = s.at(x, y);
            const int dr = static_cast<int>(c[0]) - 128;
            const int dg = static_cast<int>(c[1]) - 128;
            if (std::abs(dr) > tol || std::abs(dg) > tol) { ++n; }
        }
    }
    return n;
}

/// Counts texels that are not exactly the encoded neutral.
auto non_neutral_texels(const test_surface& s) -> int {
    int n = 0;
    for (int y = 0; y < s.height(); ++y) {
        for (int x = 0; x < s.width(); ++x) {
            if (s.at(x, y) != NEUTRAL) { ++n; }
        }
    }
    return n;
}

/// Runs the generator over the whole of `src` into a freshly sentinel-filled `dst`.
auto run_gen(const test_surface& src, test_surface& dst, const lighting::normal_gen_params& p = {})
    -> lighting::normal_gen_stats {
    dst.fill(SENTINEL);
    return lighting::generate_sprite_normal({
        .src = src.get(),
        .rect = SDL_Rect{0, 0, src.width(), src.height()},
        .dst = dst.get(),
        .dst_at = SDL_Point{0, 0},
        .params = p,
    });
}

constexpr int TILE = 32;

} // namespace

TEST_CASE("exact_edt is exact euclidean, not a chamfer approximation", "[normal_gen]") {
    SECTION("3x3 with a single unset corner") {
        // Distances are to the nearest UNSET pixel, here only (0,0).
        const std::vector<std::uint8_t> mask{0, 1, 1, 1, 1, 1, 1, 1, 1};
        const auto d = lighting::exact_edt(mask, 3, 3);
        REQUIRE(d.size() == 9);
        CHECK(d[0] == Catch::Approx(0.0f));
        CHECK(d[1] == Catch::Approx(1.0f));
        CHECK(d[2] == Catch::Approx(2.0f));
        CHECK(d[3] == Catch::Approx(1.0f));
        // THE DISCRIMINATOR. (1,1) sits one pixel diagonally from the seed, so the true
        // euclidean distance is sqrt(2). A Manhattan/city-block pass reports 2.0, and a
        // 4-neighbour chamfer reports 2.0 as well; only an exact transform gives 1.414.
        CHECK(d[4] == Catch::Approx(std::sqrt(2.0f)));
        CHECK(d[4] < 1.5f);
        // (2,1) is sqrt(5) = 2.236. Manhattan says 3.0; a chamfer with diagonal cost
        // sqrt(2) says 1 + sqrt(2) = 2.414. Both are outside Approx's tolerance.
        CHECK(d[5] == Catch::Approx(std::sqrt(5.0f)));
        CHECK(d[5] < 2.3f);
        CHECK(d[6] == Catch::Approx(2.0f));
        CHECK(d[7] == Catch::Approx(std::sqrt(5.0f)));
        CHECK(d[8] == Catch::Approx(std::sqrt(8.0f)));
    }

    SECTION("5x5 3-4-5 triangle lands on exactly 5") {
        std::vector<std::uint8_t> mask(25, 1);
        mask[0] = 0;
        const auto d = lighting::exact_edt(mask, 5, 5);
        REQUIRE(d.size() == 25);
        // A chamfer(1, sqrt2) pass walks 3 diagonals plus 1 straight step and reports
        // 3 * 1.414 + 1 = 5.243, which is 4.9% high. The exact transform says 5.
        CHECK(d[3 * 5 + 4] == Catch::Approx(5.0f));
        CHECK(d[4 * 5 + 3] == Catch::Approx(5.0f));
        CHECK(d[4 * 5 + 4] == Catch::Approx(std::sqrt(32.0f)));
    }

    SECTION("column pass and row pass are consistent under transpose") {
        // Non-square and asymmetric, so an x/y swap inside the two-pass loop cannot
        // hide. Two seeds in opposite corners force both passes to contribute.
        constexpr int w = 4;
        constexpr int h = 6;
        std::vector<std::uint8_t> mask(w * h, 1);
        mask[0 * w + 3] = 0;
        mask[5 * w + 0] = 0;
        std::vector<std::uint8_t> flipped(w * h, 1);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) { flipped[x * h + y] = mask[y * w + x]; }
        }
        const auto d = lighting::exact_edt(mask, w, h);
        const auto dt = lighting::exact_edt(flipped, h, w);
        REQUIRE(d.size() == static_cast<std::size_t>(w * h));
        REQUIRE(dt.size() == static_cast<std::size_t>(w * h));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) { CHECK(d[y * w + x] == Catch::Approx(dt[x * h + y])); }
        }
        // Spot-check two hand values so a transform that is symmetrically wrong still
        // fails: (0,0) is 3 columns from its seed, (2,2) is sqrt(1+4) from (3,0).
        CHECK(d[0] == Catch::Approx(3.0f));
        CHECK(d[2 * w + 2] == Catch::Approx(std::sqrt(5.0f)));
    }

    SECTION("a mask with no unset pixel has no boundary and comes back zeroed") {
        const std::vector<std::uint8_t> mask(16, 1);
        const auto d = lighting::exact_edt(mask, 4, 4);
        REQUIRE(d.size() == 16);
        for (const float v : d) { CHECK(v == Catch::Approx(0.0f)); }
    }
}

TEST_CASE("coherence_gap_run separates structure from noise", "[normal_gen]") {
    const lighting::normal_gen_params p;

    SECTION("one axis-aligned contour line scores far above coh_hi") {
        constexpr int w = 8;
        constexpr int h = 8;
        std::vector<std::uint8_t> mask(w * h, 0);
        for (int x = 0; x < w; ++x) { mask[x] = 1; }
        // Row scan: 7 clean rows, one run of 8 each -> 8. Column scan: every column has
        // one run of 7 -> 7. Mean of the two axes: 7.5.
        CHECK(lighting::coherence_gap_run(mask, w, h) == Catch::Approx(7.5f));
        CHECK(lighting::coherence_gap_run(mask, w, h) > p.coh_hi);

        // Rotating the same structure 90 degrees must score identically -- the statistic
        // averages both axes precisely so orientation does not decide the gate.
        std::vector<std::uint8_t> rotated(w * h, 0);
        for (int y = 0; y < h; ++y) { rotated[y * w] = 1; }
        CHECK(lighting::coherence_gap_run(rotated, w, h) == Catch::Approx(7.5f));
    }

    SECTION("a saturated mask has no gaps at all and scores zero") {
        // This is what a 1px dither actually produces: every pixel differs from every
        // neighbour, so the edge mask is solid and there is no gap to measure.
        const std::vector<std::uint8_t> mask(64, 1);
        CHECK(lighting::coherence_gap_run(mask, 8, 8) == Catch::Approx(0.0f));
        CHECK(lighting::coherence_gap_run(mask, 8, 8) <= p.coh_lo);
    }

    SECTION("a 1px checkerboard mask scores 1 -- below coh_lo") {
        constexpr int w = 8;
        constexpr int h = 8;
        std::vector<std::uint8_t> mask(w * h, 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) { mask[y * w + x] = (x + y) % 2 == 0 ? 1 : 0; }
        }
        CHECK(lighting::coherence_gap_run(mask, w, h) == Catch::Approx(1.0f));
        CHECK(lighting::coherence_gap_run(mask, w, h) <= p.coh_lo);
    }

    SECTION("isolated speckles score HIGH -- the documented hole in the statistic") {
        // Gap length RISES as edges get sparser, so three stray pixels look more
        // structured than brick does. The statistic cannot close this; `min_density`
        // does, and the generator test below pins that.
        std::vector<std::uint8_t> mask(TILE * TILE, 0);
        mask[5 * TILE + 5] = 1;
        mask[9 * TILE + 20] = 1;
        mask[26 * TILE + 11] = 1;
        // 29 clean rows of 32 plus 6 partial runs (5,26,20,11,11,20) -> 1021/35, and the
        // column scan is arithmetically identical.
        CHECK(lighting::coherence_gap_run(mask, TILE, TILE) == Catch::Approx(1021.0f / 35.0f));
        CHECK(lighting::coherence_gap_run(mask, TILE, TILE) > p.coh_hi);
    }
}

TEST_CASE("the full-tile gate drops the silhouette EDT", "[normal_gen]") {
    // A fully opaque uniform tile is the shape of every BN terrain sprite. Without the
    // gate the alpha EDT degenerates to distance-from-tile-border and stamps an
    // identical pyramid on every tile -- a diamond crease locked to the tile grid.
    test_surface src(TILE, TILE);
    REQUIRE(src.get() != nullptr);
    src.fill(rgba{120, 110, 100, 255});
    test_surface dst(TILE, TILE);
    REQUIRE(dst.get() != nullptr);

    const auto st = run_gen(src, dst);

    CHECK(st.full_tile);
    CHECK(st.flat);
    CHECK(st.amplitude == Catch::Approx(0.0f));
    // Every texel must be the encoded flat normal with zero blend weight. The sentinel
    // prefill means this also proves the generator wrote the whole rect.
    CHECK(non_neutral_texels(dst) == 0);
    CHECK(dst.at(0, 0) == NEUTRAL);
    CHECK(dst.at(TILE / 2, TILE / 2) == NEUTRAL);
    CHECK(dst.at(TILE - 1, TILE - 1) == NEUTRAL);
}

TEST_CASE("the coherence gate keeps brick and rejects dither", "[normal_gen]") {
    // This is the core claim of the whole feature: structured art gets relief, noise
    // stays flat, and the discriminator is a SHAPE statistic rather than a colour
    // constant (both tiles below saturate any per-pixel colour delta).
    const lighting::normal_gen_params p;

    SECTION("brick: horizontal mortar every 8px scores above coh_hi and gets relief") {
        test_surface src(TILE, TILE);
        REQUIRE(src.get() != nullptr);
        src.paint([](int, int y) -> rgba {
            return y % 8 == 0 ? rgba{200, 200, 195, 255} : rgba{140, 70, 55, 255};
        });
        test_surface dst(TILE, TILE);
        REQUIRE(dst.get() != nullptr);

        const auto st = run_gen(src, dst, p);

        CHECK(st.full_tile);
        CHECK_FALSE(st.flat);
        CHECK(st.density > p.min_density);
        CHECK(st.coherence > p.coh_hi);
        CHECK(st.amplitude == Catch::Approx(1.0f));
        // Real relief over most of the tile, not a couple of rim texels.
        CHECK(relief_texels(dst, 8) > TILE * TILE / 2);
        // Amplitude 1 must reach the blend-weight channel at full strength.
        CHECK(dst.at(TILE / 2, TILE / 2)[2] == 255);
        // The mortar runs horizontally, so all the slope is in y: nx must stay neutral.
        // A gx/gy swap in the Sobel would move this instead of G.
        for (int y = 0; y < TILE; ++y) {
            CHECK(std::abs(static_cast<int>(dst.at(TILE / 2, y)[0]) - 128) <= 1);
        }
    }

    SECTION("1px checkerboard dither scores below coh_lo and stays perfectly flat") {
        test_surface src(TILE, TILE);
        REQUIRE(src.get() != nullptr);
        src.paint([](int x, int y) -> rgba {
            return (x + y) % 2 == 0 ? rgba{90, 90, 90, 255} : rgba{150, 150, 150, 255};
        });
        test_surface dst(TILE, TILE);
        REQUIRE(dst.get() != nullptr);

        const auto st = run_gen(src, dst, p);

        CHECK(st.full_tile);
        CHECK_FALSE(st.flat);            // it has plenty of colour variation...
        CHECK(st.density > 0.9f);        // ...and saturates the edge mask completely...
        CHECK(st.coherence <= p.coh_lo); // ...but carries no shape.
        CHECK(st.amplitude == Catch::Approx(0.0f));
        // Refuses to fabricate relief rather than producing "cottage cheese".
        CHECK(non_neutral_texels(dst) == 0);
    }
}

TEST_CASE("the sparse gate refuses to invent relief from speckles", "[normal_gen]") {
    // A FULLY OPAQUE tile with three off-colour pixels. It must be full-tile: for a
    // cut-out sprite `slope` floors at `ext_weight`, so the silhouette bevel legitimately
    // survives an amplitude of 0 and there would be nothing to assert.
    test_surface src(TILE, TILE);
    REQUIRE(src.get() != nullptr);
    src.paint([](int x, int y) -> rgba {
        const bool spot = (x == 5 && y == 5) || (x == 20 && y == 9) || (x == 11 && y == 26);
        return spot ? rgba{240, 240, 240, 255} : rgba{100, 100, 100, 255};
    });
    test_surface dst(TILE, TILE);
    REQUIRE(dst.get() != nullptr);

    const lighting::normal_gen_params p;
    const auto st = run_gen(src, dst, p);

    CHECK(st.full_tile);
    CHECK_FALSE(st.flat);
    CHECK(st.density < p.min_density);
    CHECK(st.coherence == Catch::Approx(0.0f));
    CHECK(st.amplitude == Catch::Approx(0.0f));
    // THE FAILURE THIS GATE EXISTS TO STOP: the EDT growing big smooth domes out of
    // stray pixels, i.e. geometry that is not in the art at all.
    CHECK(non_neutral_texels(dst) == 0);

    SECTION("with min_density disabled the same tile DOES invent a blob") {
        // Proves the assertion above is testing the gate and not a fixture that simply
        // cannot produce relief. Three speckles leave huge clean gaps, so the coherence
        // statistic scores them as highly structured and the gate would pass them.
        auto open = p;
        open.min_density = 0.0f;
        const auto loose = run_gen(src, dst, open);
        CHECK(loose.coherence > p.coh_hi);
        CHECK(loose.amplitude == Catch::Approx(1.0f));
        CHECK(relief_texels(dst, 8) > 0);
    }
}

TEST_CASE("the flat gate short-circuits a sprite with no contours", "[normal_gen]") {
    // Colour variation of 5/255 = 0.0196, below flat_eps 0.04 but not zero -- a gate
    // written as `== 0` would let this through.
    test_surface src(TILE, TILE);
    REQUIRE(src.get() != nullptr);
    src.paint([](int x, int) -> rgba {
        return x % 2 == 0 ? rgba{100, 100, 100, 255} : rgba{105, 100, 100, 255};
    });
    test_surface dst(TILE, TILE);
    REQUIRE(dst.get() != nullptr);

    const lighting::normal_gen_params p;

    SECTION("default params") {
        const auto st = run_gen(src, dst, p);
        CHECK(st.flat);
        CHECK(st.amplitude == Catch::Approx(0.0f));
        CHECK(non_neutral_texels(dst) == 0);
    }

    SECTION("still flat with edge_threshold below the gradient") {
        // Isolates the FLAT gate from the sparse gate: at this threshold every pixel is
        // an "edge", so density is 1.0 and sparsity cannot be what fired. `flat` is an
        // absolute statement about the art, independent of threshold tuning.
        auto low = p;
        low.edge_threshold = 0.01f;
        const auto st = run_gen(src, dst, low);
        CHECK(st.density > 0.99f);
        CHECK(st.flat);
        CHECK(st.coherence == Catch::Approx(0.0f));
        CHECK(st.amplitude == Catch::Approx(0.0f));
        CHECK(non_neutral_texels(dst) == 0);
    }
}

TEST_CASE("normal encoding pins the axis and the sign", "[normal_gen]") {
    // One contour line near an edge of a full opaque tile. The internal EDT peaks away
    // from the contour, so the height map ramps up from the contour into the large
    // region and down into the small one -- a known one-sided ramp on each side.
    //
    // The cross axis is asserted within +/-1 rather than exactly 128: its gradient is a
    // floating-point residue of an exact cancellation, and `floor(x + 0.5)` at exactly
    // 128.0 drops to 127 for any negative epsilon. A real signal is 13 code values away.
    const lighting::normal_gen_params p;

    SECTION("a HORIZONTAL contour moves G only") {
        test_surface src(TILE, TILE);
        REQUIRE(src.get() != nullptr);
        src.paint([](int, int y) -> rgba {
            return y == 4 ? rgba{230, 230, 230, 255} : rgba{90, 80, 70, 255};
        });
        test_surface dst(TILE, TILE);
        REQUIRE(dst.get() != nullptr);

        const auto st = run_gen(src, dst, p);
        REQUIRE(st.amplitude == Catch::Approx(1.0f));

        // Below the contour the height rises with y, so gy > 0 and ny < 0: G below 128.
        for (const int y : {10, 16, 20, 25}) {
            const auto c = dst.at(TILE / 2, y);
            CHECK(static_cast<int>(c[1]) < 124);
            CHECK(std::abs(static_cast<int>(c[0]) - 128) <= 1);
        }
        // Above it the ramp reverses, so the sign must reverse too. Asserting both sides
        // pins the sign absolutely -- a global negation would fail one of them.
        const auto above = dst.at(TILE / 2, 1);
        CHECK(static_cast<int>(above[1]) > 132);
        CHECK(std::abs(static_cast<int>(above[0]) - 128) <= 1);
    }

    SECTION("a VERTICAL contour moves R only") {
        test_surface src(TILE, TILE);
        REQUIRE(src.get() != nullptr);
        src.paint([](int x, int) -> rgba {
            return x == 4 ? rgba{230, 230, 230, 255} : rgba{90, 80, 70, 255};
        });
        test_surface dst(TILE, TILE);
        REQUIRE(dst.get() != nullptr);

        const auto st = run_gen(src, dst, p);
        REQUIRE(st.amplitude == Catch::Approx(1.0f));

        for (const int x : {10, 16, 20, 25}) {
            const auto c = dst.at(x, TILE / 2);
            CHECK(static_cast<int>(c[0]) < 124);
            CHECK(std::abs(static_cast<int>(c[1]) - 128) <= 1);
        }
        const auto left = dst.at(1, TILE / 2);
        CHECK(static_cast<int>(left[0]) > 132);
        CHECK(std::abs(static_cast<int>(left[1]) - 128) <= 1);
    }
}

TEST_CASE("transparent texels carry zero blend weight", "[normal_gen]") {
    // A cut-out sprite: 8px transparent margin around structured art. This is also the
    // only path that exercises the external (silhouette) EDT, since the full-tile gate
    // suppresses it everywhere else.
    test_surface src(TILE, TILE);
    REQUIRE(src.get() != nullptr);
    const auto inside = [](int x, int y) { return x >= 8 && x <= 23 && y >= 8 && y <= 23; };
    src.paint([&inside](int x, int y) -> rgba {
        if (!inside(x, y)) { return rgba{0, 0, 0, 0}; }
        return y % 4 == 0 ? rgba{200, 200, 195, 255} : rgba{140, 70, 55, 255};
    });
    test_surface dst(TILE, TILE);
    REQUIRE(dst.get() != nullptr);

    const auto st = run_gen(src, dst);

    CHECK_FALSE(st.full_tile);
    REQUIRE(st.amplitude == Catch::Approx(1.0f));

    for (int y = 0; y < TILE; ++y) {
        for (int x = 0; x < TILE; ++x) {
            const auto c = dst.at(x, y);
            if (inside(x, y)) {
                // Opaque: the sprite-level gate result reaches the weight channel.
                CHECK(c[2] == 255);
            } else {
                // Outside the silhouette the shader must keep its own alpha bevel, which
                // is the only place it produces a non-flat normal today. Blending a hard
                // (0,0,1) over it at full weight would delete that relief, so the weight
                // is 0 here even though the sprite's amplitude is 1.
                CHECK(c == NEUTRAL);
            }
        }
    }
}

TEST_CASE("generate_sprite_normal refuses to write outside its surfaces", "[normal_gen]") {
    test_surface src(TILE, TILE);
    REQUIRE(src.get() != nullptr);
    src.fill(rgba{200, 40, 40, 255});
    test_surface dst(TILE, TILE);
    REQUIRE(dst.get() != nullptr);

    const auto untouched = [&dst]() -> bool {
        for (int y = 0; y < TILE; ++y) {
            for (int x = 0; x < TILE; ++x) {
                if (dst.at(x, y) != SENTINEL) { return false; }
            }
        }
        return true;
    };

    SECTION("a source rect past the edge of the sheet") {
        dst.fill(SENTINEL);
        lighting::generate_sprite_normal({
            .src = src.get(),
            .rect = SDL_Rect{16, 16, TILE, TILE},
            .dst = dst.get(),
            .dst_at = SDL_Point{0, 0},
            .params = {},
        });
        CHECK(untouched());
    }

    SECTION("a destination offset past the edge of the page") {
        dst.fill(SENTINEL);
        lighting::generate_sprite_normal({
            .src = src.get(),
            .rect = SDL_Rect{0, 0, TILE, TILE},
            .dst = dst.get(),
            .dst_at = SDL_Point{1, 0},
            .params = {},
        });
        CHECK(untouched());
    }

    SECTION("a null source") {
        dst.fill(SENTINEL);
        const auto st = lighting::generate_sprite_normal({
            .src = nullptr,
            .rect = SDL_Rect{0, 0, TILE, TILE},
            .dst = dst.get(),
            .dst_at = SDL_Point{0, 0},
            .params = {},
        });
        CHECK(untouched());
        CHECK(st.amplitude == Catch::Approx(0.0f));
    }
}
