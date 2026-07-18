// Debug-line vertex shader — instanced line-list draw from structured buffer.
// Each instance is one line segment (2 vertices); SV_VertexID 0 = start,
// 1 = end.  World-tile coordinates are converted to NDC using camera params.

struct LineSegment {
    float ax, ay;     // start point (world tile coords)
    float bx, by;     // end point (world tile coords)
    float r, g, b, a; // RGBA colour
};

StructuredBuffer<LineSegment> Lines : register( t0, space0 );

cbuffer FrameParams : register( b0, space1 ) {
    float cam_x, cam_y;       // camera origin (tile units)
    float tile_w, tile_h;     // tile size (pixels)
    float target_w, target_h; // render target size (pixels)
    float pad0, pad1;
};

struct VS_OUT {
    float4 pos    : SV_POSITION;
    float4 colour : TEXCOORD0;
};

VS_OUT main( uint vid : SV_VertexID, uint iid : SV_InstanceID )
{
    const LineSegment seg = Lines[iid];

    // Select endpoint: vertex 0 = start, vertex 1 = end.
    const float wx = ( vid == 0 ) ? seg.ax : seg.bx;
    const float wy = ( vid == 0 ) ? seg.ay : seg.by;

    // World-tile → screen-pixel → NDC.
    // Matches sprite.vert.hlsl's: ndc = dst_px / target * 2 - 1
    const float px = ( wx - cam_x ) * tile_w;
    const float py = ( wy - cam_y ) * tile_h;
    const float ndc_x =  ( px / target_w ) * 2.0 - 1.0;
    const float ndc_y = -( ( py / target_h ) * 2.0 - 1.0 );

    VS_OUT o;
    o.pos    = float4( ndc_x, ndc_y, 0.0, 1.0 );
    o.colour = float4( seg.r, seg.g, seg.b, seg.a );
    return o;
}
