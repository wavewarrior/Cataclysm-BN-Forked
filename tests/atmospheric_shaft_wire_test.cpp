// Wire-format gate for the C++ <-> HLSL god-ray shaft + dust-mote instance
// records (Step 6, atmospheric-lighting-coherence plan).
//
// `lighting::godray_shaft_instance` (src/lighting/godray_shaft_pass.h) and
// `lighting::dust_mote_instance` (src/lighting/dust_mote_effect.h) are each
// declared a second time, VERBATIM and with no `#include` to share it, in
// data/shaders/lighting/src/godray_shaft.vert.hlsl and dust_mote.vert.hlsl.
// Both are scalar-only, tightly-packed StructuredBuffer records — see
// tests/sprite_instance_wire_test.cpp's header comment for the exact failure
// mode a silent reorder/retype causes (a shader read that steps into its
// neighbour's fields, unnoticed because most default values render as a
// plausible-looking near-zero effect rather than a crash). This test is that
// missing gate for the two new Step 6 passes.
//
// Tagged `[lighting]` only — deliberately NOT `[.gpu]`: it merely reads text
// files, so it must run in the ordinary cata_test-tiles lane.

#include "catch/catch_amalgamated.hpp"
#include "lighting/dust_mote_effect.h"
#include "lighting/godray_shaft_pass.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

/// One declarator from an HLSL struct body, in declaration order.
struct hlsl_field {
    std::string type;
    std::string name;
};

/// tests/test_main.cpp runs with the repo root as CWD, but no other test
/// reads out of `data/`, so a couple of parent-relative fallbacks are tried
/// before giving up — and giving up names every path attempted rather than
/// yielding an empty list (mirrors sprite_instance_wire_test.cpp).
auto read_shader_source(const std::string& relative_path) -> std::string {
    const std::string prefixes[] = {"", "../", "../../"};
    std::string tried;
    for (const std::string& prefix : prefixes) {
        const std::string path = prefix + relative_path;
        std::ifstream file(path, std::ios::binary);
        if (file.good()) {
            return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        }
        tried += (tried.empty() ? "" : ", ") + path;
    }
    FAIL("could not open shader source; tried: "
         << tried << " (tests must run with the repo root as CWD)");
    return {};
}

/// Drops `//` line comments and `/* */` block comments.
auto strip_comments(const std::string& src) -> std::string {
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size();) {
        if (src.compare(i, 2, "//") == 0) {
            while (i < src.size() && src[i] != '\n') { ++i; }
        } else if (src.compare(i, 2, "/*") == 0) {
            const auto end = src.find("*/", i + 2);
            i = (end == std::string::npos) ? src.size() : end + 2;
        } else {
            out.push_back(src[i]);
            ++i;
        }
    }
    return out;
}

/// Extracts the declarators of `struct <struct_name> { ... };` in order.
/// Fails loudly if the struct or its terminator cannot be found.
auto parse_hlsl_struct(const std::string& path, const std::string& struct_name)
    -> std::vector<hlsl_field> {
    const std::string src = strip_comments(read_shader_source(path));

    const auto decl = src.find("struct " + struct_name);
    INFO("no `struct " << struct_name << "` declaration in " << path);
    REQUIRE(decl != std::string::npos);

    const auto open = src.find('{', decl);
    INFO("unterminated `struct " << struct_name << "` (no `{`) in " << path);
    REQUIRE(open != std::string::npos);

    const auto close = src.find('}', open);
    INFO("unterminated `struct " << struct_name << "` (no `}`) in " << path);
    REQUIRE(close != std::string::npos);

    std::vector<hlsl_field> fields;
    std::istringstream body(src.substr(open + 1, close - open - 1));
    std::string statement;
    while (std::getline(body, statement, ';')) {
        std::istringstream words(statement);
        std::string type;
        if (!(words >> type)) { continue; }
        std::string rest;
        std::getline(words, rest);
        std::istringstream names(rest);
        std::string name;
        while (std::getline(names, name, ',')) {
            const auto begin = name.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) { continue; }
            const auto end = name.find_last_not_of(" \t\r\n");
            fields.push_back({type, name.substr(begin, end - begin + 1)});
        }
    }

    INFO("parsed no fields at all from `struct " << struct_name << "` in " << path);
    REQUIRE(!fields.empty());
    return fields;
}

/// Compares a parsed declaration against the canonical field-name list,
/// checking every field is `float` (a same-width int/uint reinterprets bits)
/// and naming the first differing index so a reorder is as loud as a
/// truncation.
void check_matches_canonical(
    const std::string& path, const std::string& struct_name, const std::vector<hlsl_field>& fields,
    const std::vector<std::string>& canonical_fields) {
    INFO(path << " `" << struct_name << "` declares " << fields.size()
              << " fields; canonical wire format has " << canonical_fields.size());
    CHECK(fields.size() == canonical_fields.size());

    const auto shared = std::min(fields.size(), canonical_fields.size());
    for (std::size_t i = 0; i < shared; ++i) {
        if (fields[i].name != canonical_fields[i]) {
            FAIL(path << ": first differing field at index " << i << " — declares `"
                      << fields[i].name << "`, canonical wire format has `" << canonical_fields[i]
                      << "`");
        }
        if (fields[i].type != "float") {
            FAIL(path << ": field " << i << " (`" << fields[i].name << "`) is declared `"
                      << fields[i].type
                      << "`, must be `float` — a non-float of the same width "
                         "silently reinterprets the wire bits");
        }
    }
}

const std::string godray_vert_path = "data/shaders/lighting/src/godray_shaft.vert.hlsl";
const std::string dust_vert_path = "data/shaders/lighting/src/dust_mote.vert.hlsl";

/// THE SINGLE SOURCE OF TRUTH for the god-ray shaft instance wire format.
/// Adding, removing, renaming or REORDERING a field means editing all THREE
/// declarations together: godray_shaft_pass.h, godray_shaft.vert.hlsl, and
/// this list.
const std::vector<std::string> godray_canonical_fields = {
    "cx", "cy", "dir_x", "dir_y", "length_px", "half_width_px", "r", "g", "b", "strength",
};

/// THE SINGLE SOURCE OF TRUTH for the dust mote instance wire format.
const std::vector<std::string> dust_canonical_fields = {
    "cx", "cy", "radius_px", "r", "g", "b", "alpha", "pad0",
};

} // namespace

TEST_CASE("godray_shaft_instance wire format matches godray_shaft.vert.hlsl", "[lighting]") {
    const auto fields = parse_hlsl_struct(godray_vert_path, "GodrayShaftInstance");
    check_matches_canonical(
        godray_vert_path, "GodrayShaftInstance", fields, godray_canonical_fields);

    SECTION("the canonical list matches the real C++ struct") {
        // C++ has no reflection, so field ORDER cannot be verified here (pinned
        // by `sizeof` plus code review); the HLSL side is pinned exactly.
        CHECK(sizeof(lighting::godray_shaft_instance) == 4 * godray_canonical_fields.size());
        CHECK(alignof(lighting::godray_shaft_instance) == alignof(float));
    }
}

TEST_CASE("dust_mote_instance wire format matches dust_mote.vert.hlsl", "[lighting]") {
    const auto fields = parse_hlsl_struct(dust_vert_path, "DustMoteInstance");
    check_matches_canonical(dust_vert_path, "DustMoteInstance", fields, dust_canonical_fields);

    SECTION("the canonical list matches the real C++ struct") {
        CHECK(sizeof(lighting::dust_mote_instance) == 4 * dust_canonical_fields.size());
        // dust_mote_effect.h's `pad0` exists to keep the record 16-byte aligned,
        // which the vertex shader's StructuredBuffer<DustMoteInstance> binding
        // requires; a size that is not a multiple of 16 breaks that.
        CHECK(sizeof(lighting::dust_mote_instance) % 16 == 0);
        CHECK(alignof(lighting::dust_mote_instance) == alignof(float));
    }
}
