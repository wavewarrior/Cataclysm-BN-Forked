#include "tts_voice_registry.h"

#ifdef COOP_ENABLED

#include "debug.h"
#include "npc_class.h"

void tts_voice_registry::register_voice( const npc_class_id &npc_type,
        const std::string &voice_name )
{
    if( npc_type.is_null() ) {
        debugmsg( "TTS: refusing to register voice for null npc_class_id" );
        return;
    }
    voices_[npc_type.str()] = voice_name;
}

std::optional<std::string> tts_voice_registry::get_voice( const npc_class_id &npc_type ) const
{
    if( npc_type.is_null() ) {
    return std::nullopt;
}
const auto it = voices_.find( npc_type.str() );
if( it == voices_.end() ) {
    return std::nullopt;
}
return it->second;
}

std::optional<std::string> tts_voice_registry::resolve_voice( const npc &npc_instance ) const
{
    // Priority 1: Explicit registry entry for this NPC's class
    auto voice = get_voice( npc_instance.myclass );
    if( voice ) {
        return voice;
    }

    // Priority 2: npc_template override (via string_id lookup)
    const npc_template_id tmpl_id( npc_instance.idz );
    if( tmpl_id.is_valid() && !tmpl_id->voice_pack_id.empty() ) {
        return tmpl_id->voice_pack_id;
    }

    // Priority 3: npc_class voice_pack_id
    if( npc_instance.myclass.is_valid() && !npc_instance.myclass->get_voice_pack_id().empty() ) {
        return npc_instance.myclass->get_voice_pack_id();
    }

    return std::nullopt;
}


void tts_voice_registry::unregister_voice( const npc_class_id &npc_type )
{
    if( npc_type.is_null() ) {
        return;
    }
    voices_.erase( npc_type.str() );
}

void tts_voice_registry::clear()
{
    voices_.clear();
}

tts_voice_registry &tts_voice_registry::instance()
{
    static tts_voice_registry reg;
    return reg;
}

#endif // COOP_ENABLED