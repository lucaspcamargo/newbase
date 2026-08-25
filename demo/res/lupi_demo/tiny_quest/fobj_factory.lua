-- fobj_factory : given a tileset and tile, knows to build the correct object for that
-- we keep the "database" here in the source for simplicity
-- should fallback to a default, sane standard

FObjTile = require("fobj_tile")
--FObjWater = require("fobj_water")

local TYPE_MAP

local BUILD_DATA = {
    -- dados para nosso tileset
    basictiles = {
        [29] = { -- água
            type = "water"
        } 
    }
}

local TYPE_CACHE = {}

local function cache_try_find(type_key)
    -- try to locate a type via require("fobj_$type")
    -- in case require errors out, we cache an empty table {}
    -- otherwise, cache contains {class=FObj_Type}
    if TYPE_CACHE[type_key] ~= nil then
        return TYPE_CACHE[type_key]
    end

    local ok, class = pcall(require, "fobj_" .. type_key)
    if ok then
        TYPE_CACHE[type_key] = { class = class }
    else
        TYPE_CACHE[type_key] = {}
    end

    return TYPE_CACHE[type_key]
end

-- builds a field object from a tile in the "obj" layer
-- ts_data contains specific tile data given tile index, and must also contain a sprite ref (key "sprite")
function fobj_build(tileset, ts_data, tidx, x, y)

    ts_build_data = BUILD_DATA[tileset]
    if ts_build_data == nil then
        return nil
    end

    instr = ts_build_data[tidx]
    if instr == nil then
        return FObjTile:new( x, y, ts_data.sprite, tidx )
    end

    -- we have valid instructions
    type_key = instr.type
    type_ref = cache_try_find(type_key)

    if type_ref.class ~= nil then
        -- create fobj of specific type
        return (type_ref.class):new( x, y, ts_data.sprite, tidx )
    else
        print("[fobj_factory] WARNING: cannot find fobj type: "..instr.type)
    end
end