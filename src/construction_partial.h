#pragma once

#include "item.h"
#include "locations.h"
#include "type_id.h"

#include <list>

struct partial_con {
    explicit partial_con(const tripoint_bub_ms& loc, const dimension_id& dim)
        : components(new partial_con_item_location(loc, dim)) {}
    explicit partial_con(const tripoint_abs_ms& loc, const dimension_id& dim)
        : components(new partial_con_item_location(loc, dim)) {}
    int counter = 0;
    location_vector<item> components;
    construction_id id = construction_id(-1);
};
