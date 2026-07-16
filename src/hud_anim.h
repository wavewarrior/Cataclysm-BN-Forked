#pragma once
#ifndef CATA_SRC_HUD_ANIM_H
#define CATA_SRC_HUD_ANIM_H

#include <map>
#include <string>

#include "sidebar_anim.h"

// Forward
namespace Rml
{
class ElementDocument;
}

// Per-element HUD tween driver: bridges sidebar_anim's data-driven spec engine
// to RmlUi DOM elements via SetProperty. Each HUD element (vital bar, target
// indicator, log row) is fed a value each sync; the driver samples transforms
// and applies them as CSS properties each frame.
//
// Supported channels: alpha (opacity), offset_y (vertical translation),
// scale, scale_y, rotation (CSS transform). Unsupported: color_blend
// (needs per-element base-color knowledge, Phase 3). That channel is
// skipped with a one-time warning.
namespace hud_anim
{

// Options for feeding a HUD element into the animation system.
struct feed_options {
    std::string element_id; // element's id attribute in sidebar_hud document
    std::string spec_icon; // icons.json "animations" spec key (e.g. "hud_vbar")
    double value = 0.0; // drives sidebar_anim trigger logic
    bool is_critical = false; // enables critical-band specs
};

/// Feed one animated HUD element.
auto feed( const feed_options &opts ) -> void;

/// Element left the DOM: drop applier bookkeeping AND the registry key.
auto forget( const std::string &element_id ) -> void;

/// Per-frame applier: sample every fed element and SetProperty on the open doc.
auto tick( Rml::ElementDocument *doc, std::uint32_t now ) -> void;

/// Clear all state (HUD close / game load).
auto clear() -> void;

} // namespace hud_anim

#endif // CATA_SRC_HUD_ANIM_H