#pragma once

// Forward declaration to avoid circular #include with item.h
class item;

#include "coordinates.h"

#include <optional>
#include <string>


enum cable_state {
    state_none = 0,
    state_self,
    state_grid,
    state_solar_pack,
    state_UPS,
    state_vehicle
};

inline const std::string p1_name = "p1";
inline const std::string p2_name = "p2";
inline const std::string source_p1_name = "source_" + p1_name;
inline const std::string source_p2_name = "source_" + p2_name;
inline constexpr tripoint_abs_ms tripoint_abs_ms_min(tripoint_min);

struct cable_connection_data {
    struct connection {
        cable_state state = state_none;
        tripoint_abs_ms point = tripoint_abs_ms_min;

        bool is_character() const { return state == state_self; }

        bool empty() const { return state == state_none; }

        bool map_point() const { return state == state_grid || state == state_vehicle; }

        bool point_valid() { return point != tripoint_abs_ms_min; }

        bool operator==(const connection& other) const {
            return state == other.state && point == other.point;
        }
    };
    connection con1{};
    connection con2{};

    bool empty() const { return con1.empty() && con2.empty(); }

    bool complete() const { return !con1.empty() && !con2.empty(); }

    bool character_only() const { return !complete() && character_connected(); }

    bool character_connected() const { return con1.is_character() || con2.is_character(); }

    bool has_map_connection() const { return con1.map_point() || con2.map_point(); }

    bool intermap_connection() const { return con1.map_point() && con2.map_point(); }

    connection* get_map_connection() {
        if (intermap_connection()) {
            return nullptr;
        } else if (con1.map_point()) {
            return &con1;
        } else if (con2.map_point()) {
            return &con2;
        }
        return nullptr;
    }

    connection* get_nonchar_connection() {
        if (!con1.is_character() && !con1.empty()) {
            return &con1;
        } else if (!con2.is_character() && !con2.empty()) {
            return &con2;
        }
        return nullptr;
    }

    void set_vars(item* const cable) const {
        if (!cable) { return; }
        if (!con1.empty()) {
            cable->set_var(p1_name, con1.state);
            if (con1.point != tripoint_abs_ms_min) {
                cable->set_var(source_p1_name, con1.point.raw());
            }
        }
        if (!con2.empty()) {
            cable->set_var(p2_name, con2.state);
            if (con2.point != tripoint_abs_ms_min) {
                cable->set_var(source_p2_name, con2.point.raw());
            }
        }
    }
    static bool ups_connected(const item* const cable);

    static void unset_vars(item* const cable) {
        unset_con1(cable);
        unset_con2(cable);
    }
    void unset_con(item* const cable, connection& con) {
        if (con == con1) {
            unset_con1(cable);
        } else if (con == con2) {
            unset_con2(cable);
        }
    }
    void unset_other_con(item* const cable, connection& con) {
        if (con == con1) {
            unset_con2(cable);
        } else if (con == con2) {
            unset_con1(cable);
        }
    }
    static void unset_con1(item* const cable) {
        if (!cable) { return; }
        cable->erase_var(p1_name);
        cable->erase_var(source_p1_name);
    }
    static void unset_con2(item* const cable) {
        if (!cable) { return; }
        cable->erase_var(p2_name);
        cable->erase_var(source_p2_name);
    }

    static std::optional<cable_connection_data> make_data(const item* const cable) {
        if (cable) {
            return make_data(*cable);
        } else {
            return std::nullopt;
        }
    }

    static std::optional<cable_connection_data> make_data(const item& cable);

    cable_connection_data(const item& cable) {

        con1.state = cable_state(cable.get_var(p1_name, 0.0));
        con2.state = cable_state(cable.get_var(p2_name, 0.0));

        auto tmp = cable.get_var(source_p1_name, tripoint_min);
        con1.point = tripoint_abs_ms(tmp);

        tmp = cable.get_var(source_p2_name, tripoint_min);
        con2.point = tripoint_abs_ms(tmp);
    }
};
