#pragma once

#include <array>
#include <string_view>

/// Test files whose cases need no initialised game: no language, options, static data, world,
/// avatar or map. When every selected case comes from one of these files, the test binary skips
/// `init_global_game_state` (about 11 s) and `g` stays null, so a stray `g->` crashes loudly.
///
/// Entries are file stems. A file belongs here only if all of its cases pass with init skipped.
inline constexpr auto logic_tier_files = std::to_array<std::string_view>({
    "algo_test",
    "atmospheric_shaft_wire_test",
    "cata_arena_test",
    "cata_libintl_test",
    "cata_tiles_canopy_test",
    "cata_utility_test",
    "catacharset_test",
    "char_validity_check_test",
    "coop_delta_test",
    "coop_fiber_test",
    "coop_input_window_test",
    "coop_modernization_test",
    "coop_net_sim_test",
    "coop_net_test",
    "coop_packets_test",
    "coop_reconcile_test",
    "coordinate_test",
    "cube_direction_test",
    "data_vars",
    "dependency_tree_test",
    "distribution_test",
    "driver_message_delta_test",
    "driver_window_test",
    "flat_set_test",
    "fold_string_test",
    "fork_runner_test",
    "frame_camera_test",
    "frame_history_test",
    "frame_plan_test",
    "hash_test",
    "hud_particle_test",
    "hud_runic_test",
    "hud_soma_figure_test",
    "lighting_emitter_cull_test",
    "lighting_settings_test",
    "line_test",
    "make_static_test",
    "map_iterator_test",
    "map_memory_test",
    "math_functions_test",
    "name_test",
    "newchar_aptitude_test",
    "newchar_balance_test",
    "newchar_bionic_gate_test",
    "newchar_finish_gate_test",
    "newchar_seal_test",
    "newchar_stat_meter_test",
    "newchar_trait_gate_test",
    "normal_gen_test",
    "path_info_test",
    "pinyin_test",
    "player_cmd_test",
    "point_test",
    "poly_serialized_test",
    "quarantine_test",
    "render_frame_compare_test",
    "requirements_test",
    "rng_test",
    "shape_test",
    "sound_mechanics_test",
    "sprite_instance_wire_test",
    "string_formatter_test",
    "string_test",
    "submap_test",
    "tile_light_mode_test",
    "tile_occlusion_test",
    "translations_test",
    "translocator_utils_test",
    "ui_tween_test",
    "units_test",
    "value_ptr_test",
});

/// True when `path` (as recorded by Catch2 for a test case) is a Logic tier file.
inline auto is_logic_tier_file(std::string_view path) -> bool {
    const auto slash = path.find_last_of("/\\");
    auto name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    if (name.ends_with(".cpp")) { name.remove_suffix(4); }
    for (const auto stem : logic_tier_files) {
        if (stem == name) { return true; }
    }
    return false;
}
