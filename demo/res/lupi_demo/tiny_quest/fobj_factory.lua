-- fobj_factory : given a tileset and tile, knows to build the correct object for that
-- we keep the "database" here in the source for simplicity
-- should fallback to a default, sane standard

FObjTile = require("fobj_tile")
--FObjWater = require("fobj_water")

local TYPE_MAP

local BUILD_DATA = {
    -- dados para nosso tileset
    basictiles = {
        [29] = { -- água, exemplo
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
-- ts_data contains specific tile data given the tile index, and must also contain a sprite ref (key "sprite")
-- extra_data is a table containing collected properties of point objects of the map, can be used as build data overrides
function fobj_build(tileset, tidx, x, y, ts_data, extra_data)

    local build_data = {}

    -- use data from BUILD_DATA as initial build data
    local ts_build_data = BUILD_DATA[tileset]
    local instr = ts_build_data and ts_build_data[tidx]
    if instr ~= nil then
        for key, value in pairs(instr) do
            build_data[key] = value
        end
    end

    -- then add in data from tileset (ts_data)
    local ts_tile_data = ts_data and ts_data[tidx]
    if ts_tile_data ~= nil then
        for key, value in pairs(ts_tile_data) do
            build_data[key] = value
        end
    end

    -- finally, override with fields from extra_data (map-tile-specific)
    for key, value in pairs(extra_data or {}) do
        build_data[key] = value
    end

    -- we now have finished object build data

    -- fallback to FObjTile if type is unspecified
    if build_data == nil or build_data.type == nil then
        return FObjTile:new( x, y, ts_data.sprite, tidx, build_data )
    end

    -- we have valid instructions
    type_key = build_data.type
    type_ref = cache_try_find(type_key)

    if type_ref.class ~= nil then
        -- create fobj of specific type
        return (type_ref.class):new(x, y, ts_data.sprite, tidx, build_data)
    else
        print("[fobj_factory] WARNING: cannot find fobj type: "..type_key)
    end
end