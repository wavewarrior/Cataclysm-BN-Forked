#pragma once


#include <optional>
#include <string>
#include <unordered_map>

#include "npc.h"

/// Singleton managing voice models mapped by NPC class type id.
/// Provides graceful degradation: if TTS is unavailable, returns nullopt.
class tts_voice_registry
{
    public:
        /// Register a voice name for a given NPC class type.
        void register_voice( const npc_class_id &npc_type, const std::string &voice_name );

        /// Look up the voice name for a given NPC class type.
        /// Returns nullopt if no voice is registered or TTS is unavailable.
        std::optional<std::string> get_voice( const npc_class_id &npc_type ) const;

        /// Resolve the voice pack for an NPC instance.
        /// Priority: (1) explicit registry entry, (2) npc_template voice_pack_id,
        /// (3) npc_class voice_pack_id. Returns nullopt if none set.
        std::optional<std::string> resolve_voice( const npc &npc_instance ) const;

        /// Remove a previously registered voice mapping.
        void unregister_voice( const npc_class_id &npc_type );

        /// Clear all registered voices.
        void clear();

        /// Access the singleton instance.
        static tts_voice_registry &instance();

    private:
        tts_voice_registry() = default;

        std::unordered_map<std::string, std::string> voices_;
};
