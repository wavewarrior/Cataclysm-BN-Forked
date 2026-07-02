#include "sidebar_anim.h"

#include "color.h"          // color_from_string
#include "fstream_utils.h"  // read_from_file_json
#include "json.h"           // JsonIn / JsonObject
#include "path_info.h"      // gfxdir
#include "sdl_wrappers.h"   // SDL_GetTicks

namespace sidebar_anim
{

namespace
{
anim_prop string_to_prop( const std::string &s )
{
    if( s == "scale_y" ) {
        return anim_prop::scale_y;
    }
    if( s == "alpha" ) {
        return anim_prop::alpha;
    }
    if( s == "offset_y" ) {
        return anim_prop::offset_y;
    }
    if( s == "rotation" ) {
        return anim_prop::rotation;
    }
    if( s == "color_blend" ) {
        return anim_prop::color_blend;
    }
    return anim_prop::scale;
}

anim_trigger string_to_trigger( const std::string &s )
{
    if( s == "on_increase" ) {
        return anim_trigger::on_increase;
    }
    if( s == "on_decrease" ) {
        return anim_trigger::on_decrease;
    }
    if( s == "critical" ) {
        return anim_trigger::critical;
    }
    if( s == "ambient" ) {
        return anim_trigger::ambient;
    }
    return anim_trigger::on_change;
}

// Resting value a property eases back to when its effect ends.
float identity_of( anim_prop p )
{
    return ( p == anim_prop::scale || p == anim_prop::scale_y || p == anim_prop::alpha )
           ? 1.0f : 0.0f;
}

// Default per-property end value for a spec that omits "from"/"to" (so a minimal
// "scale pop" spec just works): scale/alpha rest at 1, offsets/rotation/blend at 0.
} // namespace

void registry::bind_specs( std::map<std::string, std::vector<anim_spec>> specs )
{
    specs_ = std::move( specs );
}

void registry::load_specs()
{
    std::map<std::string, std::vector<anim_spec>> parsed;
    const std::string path = PATH_INFO::gfxdir() + "widgets/icons.json";
    read_from_file_json( path, [&parsed]( JsonIn & jsin ) {
        JsonObject jo = jsin.get_object();
        jo.allow_omitted_members(); // "//" comments + fields read by widget_icon
        for( const JsonObject icon : jo.get_array( "icons" ) ) {
            icon.allow_omitted_members(); // ignore "svg" here (read by widget_icon)
            const std::string id = icon.get_string( "id", std::string() );
            if( id.empty() || !icon.has_array( "animations" ) ) {
                continue;
            }
            std::vector<anim_spec> list;
            for( const JsonObject a : icon.get_array( "animations" ) ) {
                a.allow_omitted_members();
                anim_spec sp;
                sp.trigger = string_to_trigger( a.get_string( "trigger", "on_change" ) );
                sp.prop = string_to_prop( a.get_string( "property", "scale" ) );
                const float id_val = identity_of( sp.prop );
                sp.from = static_cast<float>( a.get_float( "from", id_val ) );
                sp.to = static_cast<float>( a.get_float( "to", id_val ) );
                sp.duration_ms = static_cast<std::uint32_t>( a.get_int( "duration", 300 ) );
                sp.ease = ui_tween::string_to_ease( a.get_string( "ease", "linear" ) );
                sp.loop = ui_tween::string_to_loop( a.get_string( "loop", "once" ) );
                sp.repeats = a.get_int( "repeats", 0 );
                if( a.has_string( "color" ) ) {
                    sp.blend_color = color_from_string( a.get_string( "color" ) );
                }
                sp.pivot_y = static_cast<float>( a.get_float( "pivot", 0.5 ) );
                list.push_back( sp );
            }
            if( !list.empty() ) {
                parsed[id] = list;
            }
        }
    }, true );
    bind_specs( std::move( parsed ) );
}

void registry::update( const std::string &key, const std::string &icon, double value,
                       bool is_critical, std::uint32_t now )
{
    channel_state &st = states_[key];

    // Prune finished one-shot tweens so any_active()/sample() stay cheap and
    // settled effects collapse back to identity. Infinite loops are kept.
    for( auto it = st.active.begin(); it != st.active.end(); ) {
        if( it->second.settled( now ) ) {
            it = st.active.erase( it );
        } else {
            ++it;
        }
    }

    const auto sit = specs_.find( icon );
    const std::vector<anim_spec> *specs = sit != specs_.end() ? &sit->second : nullptr;

    // Start one tween for a spec (used by ambient + critical-enter). For
    // color_blend, latch the spec's target colour onto the channel.
    const auto start = [&]( const anim_spec & sp ) {
        st.active[sp.prop] = ui_tween::tween{
            sp.from, sp.to, now, sp.duration_ms, sp.ease, sp.loop, sp.repeats };
        if( sp.prop == anim_prop::color_blend ) {
            st.blend_color = sp.blend_color;
        }
        if( sp.prop == anim_prop::scale_y ) {
            st.pivot_y = sp.pivot_y;
        }
    };

    // First sight: record state, start ambient loops, do NOT fire on_change.
    if( !st.primed ) {
        st.last_value = value;
        st.was_critical = is_critical;
        st.primed = true;
        if( specs && !st.ambient_started ) {
            for( const anim_spec &sp : *specs ) {
                if( sp.trigger == anim_trigger::ambient ) {
                    start( sp );
                }
            }
            st.ambient_started = true;
        }
        return;
    }

    // Value change -> fire on_change plus the matching directional trigger
    // (on_increase / on_decrease), retargeting from the current sampled value so a
    // rapid re-trigger doesn't snap.
    if( value != st.last_value ) {
        const bool up = value > st.last_value;
        if( specs ) {
            for( const anim_spec &sp : *specs ) {
                const bool match =
                    sp.trigger == anim_trigger::on_change ||
                    ( sp.trigger == anim_trigger::on_increase && up ) ||
                    ( sp.trigger == anim_trigger::on_decrease && !up );
                if( !match ) {
                    continue;
                }
                float from = sp.from;
                const auto ex = st.active.find( sp.prop );
                if( ex != st.active.end() ) {
                    from = ex->second.value_at( now );
                }
                st.active[sp.prop] = ui_tween::tween{
                    from, sp.to, now, sp.duration_ms, sp.ease, sp.loop, sp.repeats };
                if( sp.prop == anim_prop::color_blend ) {
                    st.blend_color = sp.blend_color;
                }
                if( sp.prop == anim_prop::scale_y ) {
                    st.pivot_y = sp.pivot_y;
                }
            }
        }
        st.last_value = value;
    }

    // Critical-band entry -> start critical specs; exit -> ease those props back.
    if( is_critical && !st.was_critical ) {
        if( specs ) {
            for( const anim_spec &sp : *specs ) {
                if( sp.trigger == anim_trigger::critical ) {
                    start( sp );
                }
            }
        }
    } else if( !is_critical && st.was_critical ) {
        if( specs ) {
            for( const anim_spec &sp : *specs ) {
                if( sp.trigger != anim_trigger::critical ) {
                    continue;
                }
                float from = identity_of( sp.prop );
                const auto ex = st.active.find( sp.prop );
                if( ex != st.active.end() ) {
                    from = ex->second.value_at( now );
                }
                // Short ease back to rest (reuse the spec's ease/duration).
                st.active[sp.prop] = ui_tween::tween{
                    from, identity_of( sp.prop ), now, sp.duration_ms, sp.ease,
                    ui_tween::tween_loop::once, 0 };
            }
        }
    }
    st.was_critical = is_critical;
}

icon_transform registry::sample( const std::string &key, std::uint32_t now ) const
{
    icon_transform out;
    const auto sit = states_.find( key );
    if( sit == states_.end() ) {
        return out;
    }
    const channel_state &st = sit->second;
    out.blend_color = st.blend_color;
    out.pivot_y = st.pivot_y;
    for( const auto &pr : st.active ) {
        const float v = pr.second.value_at( now );
        switch( pr.first ) {
            case anim_prop::scale:
                out.scale = v;
                break;
            case anim_prop::scale_y:
                out.scale_y = v;
                break;
            case anim_prop::alpha:
                out.alpha = v;
                break;
            case anim_prop::offset_y:
                out.offset_y = v;
                break;
            case anim_prop::rotation:
                out.rotation = v;
                break;
            case anim_prop::color_blend:
                out.blend = v;
                break;
        }
    }
    return out;
}

bool registry::any_active( std::uint32_t now ) const
{
for( const auto &kv : states_ ) {
    for( const auto &pr : kv.second.active ) {
            if( !pr.second.settled( now ) ) {
                return true;
            }
        }
    }
    return false;
}

void registry::clear()
{
    states_.clear();
}

registry &get()
{
    static registry r;
    return r;
}

std::uint32_t now_ms()
{
    return SDL_GetTicks();
}

} // namespace sidebar_anim

bool sidebar_requires_animation()
{
    return sidebar_anim::get().any_active( SDL_GetTicks() );
}
