#pragma once

#include <cstddef>
#include <limits>

class JsonOut;

/// The agent driver's `view`: the avatar's surroundings as an ASCII terrain grid with a legend,
/// and the creatures and items in sight as a structured list, so an oracle never parses the grid.
/// Reads the world only; it takes no game time and never changes what a later query reports.
namespace driver_view
{

/// The radius a `view` request gets when it names none, and the widest one the driver answers.
constexpr int default_radius = 5;
constexpr int max_radius = 10;

/// No limit on the bytes a view may take, beyond the caps on its own lists.
constexpr size_t unlimited = std::numeric_limits<size_t>::max();

/// Writes the members of the view into the response object `jo` is inside:
/// - `radius`: the window is `2 * radius + 1` tiles on a side, centred on the avatar.
/// - `grid`: that many rows of that many characters, north at the top. `@` is the avatar; a tile
///   the avatar cannot see, or off the loaded map, is `?`.
/// - `legend`: each character used on the grid, and what it stands for.
/// - `creatures`: what the avatar can see besides itself, nearest first: `id` (the monster's
///   type), `name`, `dx` and `dy` from the avatar (east and south positive), `hostile`.
/// - `items`: items on tiles the avatar can see, nearest first: `id` (the id `query inventory`
///   reports for the same item), `name`, `dx` and `dy`.
/// The lists are cut to keep the response within the size ceiling, nearest kept. When the
/// window itself would take more than `room` bytes it is made smaller first, down to a radius
/// of 1, and `radius` then says how far it looks. Assigns an id to every item it lists.
/// Returns true when it shrank the window or cut a list. `radius` is within 1 to `max_radius`.
auto write_view( JsonOut &jo, int radius, size_t room = unlimited ) -> bool;

} // namespace driver_view
