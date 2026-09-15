#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace nb {

// A decoded, auto-palettized spritesheet. Not an nb::resource: its pixel
// indices are only meaningful against the specific cart's lupi_palette that
// produced them at load time, so it isn't shared/cached across carts the way
// core resources are — see lupi_load_spritesheet_indexed() in loaders.cpp.
//
// `pixels` holds the FULL decoded source image (image_width wide), unlike the
// real lupi-codec which repacks only the content tiles into a compact output
// buffer. We address into the original image directly instead — cheaper (no
// repack copy) and just as correct, since tile_origin() already accounts for
// the excluded margin row/column via col_offset/row_offset. See
// lupi_load_spritesheet_indexed() for how tile_width/height, cols/rows and the
// offsets are derived from lupi-codec's magic-marker tile detection.
struct lupi_spritesheet {
    int tile_width  { 8 };
    int tile_height { 8 };
    int cols        { 0 }; // content tile columns (margin column already excluded)
    int rows        { 0 }; // content tile rows (margin row already excluded)
    int tile_count  { 0 };
    int image_width { 0 }; // row stride of `pixels`, in pixels (the full, undecoded-margin image width)
    int col_offset  { 0 }; // tile units to skip in x before tile 0's column (0 or 1)
    int row_offset  { 0 }; // tile units to skip in y before tile 0's row (0 or 1)

    // indexed pixel data for the WHOLE decoded source image (image_width wide,
    // including any margin guide tile), one palette index per pixel, row-major.
    std::vector<uint8_t> pixels;

    // pointer to the top-left pixel of tile `tile_id` within `pixels`, or nullptr if OOB.
    const uint8_t* tile_origin(int tile_id) const
    {
        if (tile_id < 0 || tile_id >= tile_count)
            return nullptr;
        int tx = col_offset + (tile_id % cols);
        int ty = row_offset + (tile_id / cols);
        return pixels.data() + (static_cast<size_t>(ty) * tile_height) * image_width
                              + (static_cast<size_t>(tx) * tile_width);
    }
};

// Lua userdata wrapper for a sprite_ref, metatable "lupi.sprite_ref".
struct lupi_sprite_ref_userdata {
    std::shared_ptr<lupi_spritesheet> sheet;
};

}
