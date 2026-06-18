#pragma once
#ifndef CATA_SRC_LIGHTING_RMLUI_PROC_TEXTURE_H
#define CATA_SRC_LIGHTING_RMLUI_PROC_TEXTURE_H

#include <cstdint>
#include <string>
#include <vector>

namespace lighting
{
// Procedurally generate an ornate "runic" 9-patch frame as RGBA8 pixels
// (row-major). Pixels are either fully opaque or fully transparent, so the
// buffer is already correct for the render interface's premultiplied-alpha
// upload (no colour fringing on the transparent center). `variant`
// deterministically seeds the pattern via an FNV-1a hash, so the same string
// always yields the same border across launches. Out params receive the
// texture dimensions.
std::vector<std::uint8_t> gen_runic_frame( const std::string &variant,
        int &out_w, int &out_h );
}  // namespace lighting

#endif // CATA_SRC_LIGHTING_RMLUI_PROC_TEXTURE_H
