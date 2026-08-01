#include "hud_anim.h"

#include <string>
#include <unordered_set>
#include <format>

#include "debug.h"
#include "rml_util.h"
#include <RmlUi/Core.h>
#define dbg(x) DebugLogFL((x), DC::SDL)

namespace hud_anim
{

namespace
{

// Bookkeeping per element: tracks whether we've set inline properties so we
// can RemoveProperty when the animation settles back to identity.
struct element_book {
    std::string spec_icon;
    bool was_animating = false;
    bool opacity_set = false;
    bool top_set = false;
    // Transform sub-prop tracking (Phase 1).
    bool scale_set = false;
    bool scale_y_set = false;
    bool rotation_set = false;
    float scale_val = 1.0f;
    float scale_y_val = 1.0f;
    float rotation_val = 0.0f;
    // Color blend tracking (Phase 3).
    bool blend_set = false;
};

std::map<std::string, element_book> g_elements;

// One-time warning flags for unsupported channels.
std::unordered_set<sidebar_anim::anim_prop> g_warned_props;

auto warn_once( sidebar_anim::anim_prop prop ) -> void
{
    if( g_warned_props.insert( prop ).second ) {
    dbg( DL::Warn ) << "hud_anim: channel "
                        << static_cast<int>( prop )
                        << " is not yet implemented (Phase 3)";
    }
}

// Options for apply_channel to stay under the 3-param limit.
struct apply_channel_opts {
    Rml::Element *el = nullptr;
    sidebar_anim::anim_prop prop = sidebar_anim::anim_prop::alpha;
    float value = 0.0f;
    bool *was_animating = nullptr;
    bool *prop_set = nullptr;
};

auto apply_channel( const apply_channel_opts &opts ) -> void
{
    switch( opts.prop ) {
    case sidebar_anim::anim_prop::alpha:
        if( opts.value != 1.0f ) {
                opts.el->SetProperty( "opacity", std::format( "{:.3f}", opts.value ) );
                *opts.prop_set = true;
                *opts.was_animating = true;
            } else if( *opts.prop_set ) {
                opts.el->RemoveProperty( "opacity" );
                *opts.prop_set = false;
                *opts.was_animating = false;
            }
            break;
        case sidebar_anim::anim_prop::offset_y:
            if( opts.value != 0.0f ) {
                if( !*opts.prop_set ) {
                    opts.el->SetProperty( "position", "relative" );
                }
                opts.el->SetProperty( "top",
                                      std::format( "{:.1f}px", opts.value ) );
                *opts.prop_set = true;
                *opts.was_animating = true;
            } else if( *opts.prop_set ) {
                opts.el->RemoveProperty( "top" );
                *opts.prop_set = false;
                *opts.was_animating = false;
            }
            break;
        default:
            warn_once( opts.prop );
            break;
    }
}

} // namespace

auto feed( const feed_options &opts ) -> void
{
    g_elements[opts.element_id].spec_icon = opts.spec_icon;
    sidebar_anim::get().update( "hud:" + opts.element_id, opts.spec_icon, opts.value,
                                opts.is_critical, sidebar_anim::now_ms() );
}

auto forget( const std::string &element_id ) -> void
{
    g_elements.erase( element_id );
    sidebar_anim::get().forget( "hud:" + element_id );
}

auto tick( Rml::ElementDocument *doc, std::uint32_t now ) -> void
{
    if( doc == nullptr ) {
        return;
    }

    for( auto & [id, book] : g_elements ) {
        Rml::Element *el = doc->GetElementById( id );
        if( el == nullptr ) {
            continue;
        }

        const auto transform = sidebar_anim::get().sample( "hud:" + id, now );

        // Apply each non-identity channel
        if( transform.alpha != 1.0f ) {
            apply_channel( { .el = el, .prop = sidebar_anim::anim_prop::alpha,
                             .value = transform.alpha,
                             .was_animating = &book.was_animating,
                             .prop_set = &book.opacity_set } );
        } else if( book.opacity_set ) {
            el->RemoveProperty( "opacity" );
            book.opacity_set = false;
            book.was_animating = false;
        }

        if( transform.offset_y != 0.0f ) {
            apply_channel( { .el = el, .prop = sidebar_anim::anim_prop::offset_y,
                             .value = transform.offset_y,
                             .was_animating = &book.was_animating,
                             .prop_set = &book.top_set } );
        } else if( book.top_set ) {
            el->RemoveProperty( "top" );
            book.top_set = false;
            book.was_animating = false;
        }
        // CSS transform: compose scale, scaleY, rotate from active channels.
        book.scale_val = transform.scale;
        book.scale_y_val = transform.scale_y;
        book.rotation_val = transform.rotation;

        const bool any_transform = ( transform.scale != 1.0f
                                     || transform.scale_y != 1.0f
                                     || transform.rotation != 0.0f );

        if( any_transform ) {
            // The whole string is rebuilt from the CURRENT sample every frame, so
            // the *_set flags below record only "something is written" — they must
            // not gate the individual terms. Setting them inside each `!= identity`
            // branch (as this used to) left a channel's flag true after it settled
            // while its term had already dropped out of the string, so the element
            // kept a stale transform until every other channel settled too.
            std::string tf;
            if( transform.scale != 1.0f ) {
                tf += std::format( "scale({}) ", transform.scale );
            }
            if( transform.scale_y != 1.0f ) {
                tf += std::format( "scaleY({}) ", transform.scale_y );
            }
            if( transform.rotation != 0.0f ) {
                tf += std::format( "rotate({}deg)", transform.rotation );
            }
            // pivot_y is what makes a squash read as a recoil from the struck side
            // rather than a symmetric pinch about the centre. It was parsed and
            // stored by sidebar_anim but never emitted, so every scale_y spec in
            // icons.json (heart/droplet/food, six of them) has been squashing from
            // the middle regardless of the pivot it asked for.
            el->SetProperty( "transform-origin",
                             std::format( "50% {}%", transform.pivot_y * 100.0f ) );
            el->SetProperty( "transform", tf );
            book.scale_set = true;
            book.scale_y_set = true;
            book.rotation_set = true;
            book.was_animating = true;
        } else if( book.scale_set || book.scale_y_set || book.rotation_set ) {
            el->RemoveProperty( "transform" );
            el->RemoveProperty( "transform-origin" );
            book.scale_set = false;
            book.scale_y_set = false;
            book.rotation_set = false;
            book.was_animating = false;
        }
        // color_blend (Phase 3): blend toward the spec's target color.
        // nc_color has no RGB components — resolve the target through the HUD
        // palette (this only ever runs on HUD elements), then override the alpha
        // channel with the blend amount.
        if( transform.blend != 0.0f && transform.blend_color != c_white ) {
            std::string hex = hud_color_to_hex( transform.blend_color );
            // hex is "#RRGGBBAA" — replace alpha with blend amount
            if( hex.size() >= 9 ) {
                const unsigned char a = static_cast<unsigned char>( transform.blend * 255 );
                hex = hex.substr( 0, 7 ) + std::format( "{:02x}", a );
            }
            el->SetProperty( "background-color", hex );
            book.blend_set = true;
            book.was_animating = true;
        } else if( book.blend_set ) {
            el->RemoveProperty( "background-color" );
            book.blend_set = false;
        }
    }
}

auto clear() -> void
{
    g_elements.clear();
}

} // namespace hud_anim