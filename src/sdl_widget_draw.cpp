#include "sdl_widget_draw.h"

#include <memory>
#include <string>

#include "color.h"
#include "cursesdef.h"     // catacurses::window
#include "cursesport.h"    // cata_cursesport::WINDOW
#include "point.h"         // point
#include "sdl_display.h"   // fontwidth, fontheight
#include "sdl_utils.h"     // curses_color_to_SDL
#include "sidebar_anim.h"  // sidebar_anim::icon_transform
#include "widget_icon.h"   // widget_icon::get, draw_widget_icon declarations

#include "lighting/render_state.h"

void draw_widget_icon( const catacurses::window &win, const point &cell,
                       const std::string &icon, const nc_color &color,
                       const sidebar_anim::icon_transform &tr )
{
    cata_cursesport::WINDOW *const w = win.get<cata_cursesport::WINDOW>();
    if( w == nullptr || fontwidth <= 0 || fontheight <= 0 ) {
        return;
    }
    const int px = fontheight;
    SDL_GPUTexture *tex = widget_icon::get( icon, px );
    if( tex == nullptr ) {
        return;
    }
    const float base = static_cast<float>( px );
    const float wpx = base * tr.scale;
    const float hpx = base * tr.scale * tr.scale_y;
    const float cell_x = static_cast<float>( ( w->pos.x + cell.x ) * fontwidth );
    const float cell_y = static_cast<float>( ( w->pos.y + cell.y ) * fontheight );
    const float x = cell_x - ( wpx - base ) * 0.5f;
    const float y = cell_y + tr.pivot_y * ( base - hpx ) + tr.offset_y;
    const SDL_Color c = curses_color_to_SDL( color );
    float r = c.r / 255.f;
    float g = c.g / 255.f;
    float b = c.b / 255.f;
    if( tr.blend > 0.f ) {
        const SDL_Color bc = curses_color_to_SDL( tr.blend_color );
        r += ( bc.r / 255.f - r ) * tr.blend;
        g += ( bc.g / 255.f - g ) * tr.blend;
        b += ( bc.b / 255.f - b ) * tr.blend;
    }
    constexpr float deg_to_rad = 0.01745329252f;
    lighting::get_render_state().queue_font_glyph(
        tex, x, y, wpx, hpx, r, g, b, tr.alpha, /*lit=*/false,
        /*rotation=*/tr.rotation * deg_to_rad );
}

void draw_widget_icon( const catacurses::window &win, const point &cell,
                       const std::string &icon, const nc_color &color )
{
    draw_widget_icon( win, cell, icon, color, sidebar_anim::icon_transform{} );
}

void draw_widget_row_highlight( const catacurses::window &win, int row, int width_cells,
                                const nc_color &color, float alpha )
{
    cata_cursesport::WINDOW *const w = win.get<cata_cursesport::WINDOW>();
    if( w == nullptr || fontwidth <= 0 || fontheight <= 0 || alpha <= 0.f || width_cells <= 0 ) {
        return;
    }
    const float x = static_cast<float>( w->pos.x * fontwidth );
    const float y = static_cast<float>( ( w->pos.y + row ) * fontheight );
    const float wpx = static_cast<float>( width_cells * fontwidth );
    const float hpx = static_cast<float>( fontheight );
    const SDL_Color c = curses_color_to_SDL( color );
    lighting::get_render_state().queue_ui_rect( x, y, wpx, hpx,
            c.r / 255.f, c.g / 255.f, c.b / 255.f, alpha );
}
