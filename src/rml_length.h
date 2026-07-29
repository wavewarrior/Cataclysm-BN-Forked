#pragma once

#include <format>
#include <string>

/// Locale-independent RCSS numeric values.
///
/// RCSS is machine-parsed markup, so its numbers MUST NOT be localised.
/// `string_format`/printf `"%f"` and `std::to_string( float )` both honour
/// LC_NUMERIC, and set_language() (language.cpp) sets LC_ALL from the user's
/// locale.  On a comma-decimal locale (en_BE, de_DE, fr_FR, ...) they emit
/// `"12,5dp"`; RmlUi then rejects the declaration and leaves the element at its
/// default position/size, which reads as a blank or mislaid UI rather than as a
/// formatting bug.  `std::format` is locale-independent unless a locale is
/// passed explicitly.
///
/// Translated, player-facing text MUST stay on `string_format`: the PO
/// placeholder contract depends on it, and there a localised decimal separator
/// is the correct behaviour.
namespace rml
{

/// `<v>%` — percentage of the containing block.
inline auto pct( float v ) -> std::string { return std::format( "{:.4f}%", v ); }

/// `<v>dp` — density-independent pixels.
inline auto dp( float v ) -> std::string { return std::format( "{:.2f}dp", v ); }

/// `<v>px` — physical pixels.
inline auto px( float v ) -> std::string { return std::format( "{:.2f}px", v ); }

} // namespace rml
