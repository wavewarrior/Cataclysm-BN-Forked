#include "avatar.h"
#include "catch/catch_amalgamated.hpp"
#include "martialarts.h"
#include "player_helpers.h"

static const itype_id itype_test_weapon1("test_weapon1");
static const itype_id itype_test_weapon2("test_weapon2");

static const matype_id test_style_ma1("test_style_ma1");

TEST_CASE("martial arts", "[martial_arts]") {
    SECTION("martial art valid weapon") {
        GIVEN("a weapon that fits the martial art") {
            CHECK(test_style_ma1->has_weapon(itype_test_weapon1));
        }

        GIVEN("a weapon that fits the martial art weapon category") {
            CHECK(test_style_ma1->has_weapon(itype_test_weapon2));
        }
    }
}
