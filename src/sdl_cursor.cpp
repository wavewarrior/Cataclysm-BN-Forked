#include "sdl_cursor.h"

#include <array>
#include <cstddef>

#include "sdl_wrappers.h"

namespace
{

constexpr size_t cursor_kind_count = 4;

auto to_sdl_system_cursor( cursor_kind kind ) -> SDL_SystemCursor
{
    switch( kind ) {
    case cursor_kind::hand:
        // "Pointer that indicates a link. Usually a pointing hand." — the
        // closest SDL3 system cursor to a classic hand/interact pointer.
        return SDL_SYSTEM_CURSOR_POINTER;
    case cursor_kind::crosshair:
        return SDL_SYSTEM_CURSOR_CROSSHAIR;
    case cursor_kind::forbidden:
        return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    case cursor_kind::arrow:
        break;
}
return SDL_SYSTEM_CURSOR_DEFAULT;
}

std::array<SDL_Cursor *, cursor_kind_count> cached_cursors{ nullptr, nullptr, nullptr, nullptr };
cursor_kind current_kind_ = cursor_kind::arrow;
bool current_kind_valid_ = false;

} // namespace

auto set_game_cursor( cursor_kind kind ) -> void
{
    if( current_kind_valid_ && kind == current_kind_ ) {
        return;
    }

    const size_t idx = static_cast<size_t>( kind );
    SDL_Cursor *&cursor = cached_cursors[idx];
    if( cursor == nullptr ) {
        cursor = SDL_CreateSystemCursor( to_sdl_system_cursor( kind ) );
        if( cursor == nullptr ) {
            return; // creation failed — leave whatever cursor is active alone
        }
    }

    SDL_SetCursor( cursor );
    current_kind_ = kind;
    current_kind_valid_ = true;
}

auto destroy_game_cursors() -> void
{
for( SDL_Cursor * &cursor : cached_cursors ) {
    if( cursor != nullptr ) {
            SDL_DestroyCursor( cursor );
            cursor = nullptr;
        }
    }
    current_kind_valid_ = false;
}
