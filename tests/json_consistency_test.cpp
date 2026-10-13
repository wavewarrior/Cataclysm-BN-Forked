#include "catch/catch_amalgamated.hpp"
#include "debug.h"
#include "mapgen.h"

// Test init skips the mapgen definitions check (DynamicDataLoader::defer_mapgen_check), 1.7 s of
// the 1.9 s consistency pass, so it is paid once, here, instead of in every process that
// initialises. Every other consistency check still runs during init.
TEST_CASE("mapgen_definitions_are_consistent", "[json][consistency]") {
    check_mapgen_definitions();
    CHECK_FALSE(debug_has_error_been_observed());
}
