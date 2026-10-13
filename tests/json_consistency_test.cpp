#include "catch/catch_amalgamated.hpp"
#include "debug.h"
#include "init.h"
#include "loading_ui.h"

// Test init skips DynamicDataLoader::check_consistency (see defer_consistency_check) so the
// roughly 3 s validation pass is paid once, here, instead of in every process that initialises.
TEST_CASE("json_data_is_consistent", "[json][consistency]") {
    auto ui = loading_ui(false);
    DynamicDataLoader::get_instance().check_consistency(ui);
    CHECK_FALSE(debug_has_error_been_observed());
}
