#pragma once

#include <newbase/res/resource.hpp>
#include <string>

namespace nb {

// A Lupi "cart": a resource directory containing a real Lupi `lupi.yaml`
// manifest (name/version/developer/public — cosmetic metadata only, parsed
// for logging) sitting next to a "game.lua" entry script (real carts always
// name it that; there's no "main:" field, the sibling name is implicit).
// Unlike our earlier invented `.lupicart` format, a real cart declares no
// asset list at all — sprites are auto-discovered from the img/+maps/
// subtrees at boot (see lupi_scan_cart_assets in loaders.cpp) and maps are
// compiled on demand from maps/*.json (see tiled_maps.cpp), both driven off
// `dir` rather than anything parsed from this manifest.
//
// Registered as an nb::resource/RTTI type (see _rtti_init_lupi in lupi.cpp)
// so it loads through the normal rman()/rloader_* pipeline, reusing
// rscript/rloader_script for the actual game.lua text.
struct rlupi_cart : public resource {
    explicit rlupi_cart(entt::id_type id = 0)
        : resource(id, entt::hashed_string{"rlupi_cart"}.value()) {}

    bool valid { false };
    std::string dir; // resource-path directory the manifest lives in, trailing slash included
    std::string main_lua_src;
    std::string chunkname;
};

}
