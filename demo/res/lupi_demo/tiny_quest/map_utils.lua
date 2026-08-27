-- Utilities for working with Tiled maps and extracting more data from them

-- Lupi has no support for Object layers, and doesn't expose tileset custom properties anywhere.
-- In addition, it currently only supports tiled maps that have embedded tilesets in them.
-- To be able to do more with them, we parse the original Tiles JSON tileset definitions (.tsj)
-- and extract tile data from them.

-- This all depends on io.open() being available. It is unclear whether that will be the case in the
-- final console. Also, this needs to be tested in Lupinho, and adjustments may have to be made.
-- I don't think the simulator is chdir'ing into /loaded_game/, where the game files live...

-- Another thing is that the processed maps may have data stripped out of the original JSON.
-- Anyway, just things to look into later.

local json = require("lib/dkjson")
local FObjTile = require("fobj_tile")
require("fobj_factory")

local TILESET_DATA = {}

MAP_NOT_SOLID = 0
MAP_SOLID = 1

function map_read_tileset(path)
    print("[map_read_tileset] reading from: ".. path)

    local file, open_err = io.open(path, "r")
    if not file then
        error("failed to open tileset " .. tostring(path) .. ": " .. tostring(open_err))
    end

    local contents = file:read("*a")
    file:close()

    local tileset, _, decode_err = json.decode(contents)
    if not tileset then
        error("failed to decode tileset " .. tostring(path) .. ": " .. tostring(decode_err))
    end

    local properties_by_tile = {}
    for _, tile in ipairs(tileset.tiles or {}) do
        local properties = {}

        for _, property in ipairs(tile.properties or {}) do
            properties[property.name] = property.value
        end

        properties_by_tile[tile.id] = properties
    end

    return properties_by_tile
end

local function map_deep_copy(value, seen)
    if type(value) ~= "table" then
        return value
    end

    seen = seen or {}
    if seen[value] then
        return seen[value]
    end

    local copy = {}
    seen[value] = copy

    for key, item in pairs(value) do
        copy[map_deep_copy(key, seen)] = map_deep_copy(item, seen)
    end

    return copy
end

function map_get_tset_data(tileset_name)
    -- cache tileset-specific metadata in TILESET_DATA, reusing it across calls
    if TILESET_DATA[tileset_name] == nil then
        local path = "./gfx/" .. tostring(tileset_name) .. ".tsj"
        local ok, data_or_err = pcall(map_read_tileset, path)
        if ok then
            TILESET_DATA[tileset_name] = data_or_err
            print("[map_get_tset_data] tileset data for " .. tostring(tileset_name) .. " is: " .. json.encode(data_or_err))
        else
            print("[map_get_tset_data] failed to load tileset " .. tostring(tileset_name) .. " from " .. path .. ": " .. tostring(data_or_err))
            TILESET_DATA[tileset_name] = {}
        end
    end

    -- also append sprite reference to tile data, for later use
    TILESET_DATA[tileset_name].sprite = Sprites.find(tileset_name)

    return TILESET_DATA[tileset_name]
end

function map_read_point_objects(path, tile_width, tile_height)
    local file, open_err = io.open(path, "r")
    if not file then
        error("failed to open map " .. tostring(path) .. ": " .. tostring(open_err))
    end

    local contents = file:read("*a")
    file:close()

    local map_data, _, decode_err = json.decode(contents)
    if not map_data then
        error("failed to decode map " .. tostring(path) .. ": " .. tostring(decode_err))
    end

    local points = {}
    tile_width = tile_width or map_data.tilewidth
    tile_height = tile_height or map_data.tileheight or tile_width
    if not tile_width or not tile_height then
        error("map " .. tostring(path) .. " has no tile dimensions")
    end

    for _, layer in ipairs(map_data.layers or {}) do
        if layer.type == "objectgroup" then
            for _, object in ipairs(layer.objects or {}) do
                if object.point == true then
                    local properties = {}
                    for _, property in ipairs(object.properties or {}) do
                        properties[property.name] = property.value
                    end

                    table.insert(points, {
                        id = object.id,
                        name = object.name,
                        type = object.type,
                        layer = layer.name,
                        x = math.floor(object.x / tile_width),
                        y = math.floor(object.y / tile_height),
                        properties = properties,
                    })
                end
            end
        end
    end

    return points
end

function map_is_solid(map, x, y)
    local map_width = map.metadata.width
    local map_height = map.metadata.height

    if x < 0 or x >= map_width or y < 0 or y >= map_height then
        return true  -- outside of map is always solid
    end

    local tile_index = y * map_width + x + 1 -- solidity array is 1-based

    return map.data.solid[tile_index] == MAP_SOLID
end

local function point_object_props_in_tile(point_objects, tile_x, tile_y)
    -- filters point objects inside this tile
    -- merges their properties to a single table and returns it
    local props = {}

    for _, point_object in ipairs(point_objects or {}) do
        if point_object.x == tile_x and point_object.y == tile_y then
            for property_name, property_value in pairs(point_object.properties or {}) do
                props[property_name] = property_value
            end
        end
    end

    return props
end

function map_prepare(original, source_path)

    -- here we make a copy of the original map, and do all the processing we need on the copy
    -- we return the copy, and any extracted data

    local map_cpy = map_deep_copy(original)

    local map_w, map_h = map_cpy.metadata.width, map_cpy.metadata.height

    if source_path then
        map_cpy.point_objects = map_read_point_objects(source_path, map_cpy.metadata.tile_size)
        print("[map_prepare] found " .. #(map_cpy.point_objects) .. " point objs in map")
    else
        map_cpy.point_objects = {}
    end

    local solidity = {}
    for tile_index = 1, map_w * map_h do
        solidity[tile_index] = MAP_NOT_SOLID
    end

    -- first, go over all tiles, in all tilesets, of layer "base"
    -- in case the tile is in tileset data, and in case it has a custom property "solid" that equals true
    -- we set the corresponding tile in the `solidity` table to MAP_SOLID
    
    local base_layer = map_cpy.base
    local base_tilesets = base_layer.tilesets

    for tset_id, tset_path in pairs(base_tilesets) do
        print("[map_prepare] processing base layer tileset '" .. tostring(tset_id) .. "' with path '" .. "'")

        local ts_data = map_get_tset_data(tset_id)

        for row = 1, map_h do
            for column = 1, map_w do
                local tile_arr_index = (row - 1) * map_w + column
                local tile_ts_index = base_layer[tset_id][tile_arr_index]

                -- check for solidity
                if tile_ts_index and ts_data[tile_ts_index] then
                    if ts_data[tile_ts_index].solid == true then
                        solidity[tile_arr_index] = MAP_SOLID
                    end
                end
            end
        end
    end


    -- now, we must process the obj layer

    local obj_layer = map_cpy.obj
    local obj_tilesets = obj_layer.tilesets
    map_cpy.fobjs = {}

    for tset_id, tset_path in pairs(obj_tilesets) do
        print("[map_prepare] processing obj layer tileset:\n" .. tostring(tset_id))

        local ts_data = map_get_tset_data(tset_id)

        for row = 1, map_h do
            for column = 1, map_w do
                local tile_arr_index = (row - 1) * map_w + column
                local tile_ts_index = obj_layer[tset_id][tile_arr_index]

                -- if we find a tile in the obj layer, we create an instance of FObjTile at the x and y position
                -- we use ts_data.sprite as the sprite
                if tile_ts_index then
                    point_obj_props = point_object_props_in_tile(map_cpy.point_objects, column-1, row-1)
                    new_obj = fobj_build(
                        tset_id,
                        tile_ts_index,
                        column - 1,
                        row - 1,
                        ts_data,
                        point_obj_props -- extra_data
                    )
                    if new_obj ~= nil then
                        table.insert(map_cpy.fobjs, new_obj)
                    end
                end
            end
        end
    end

    map_cpy.data = {
        solid = solidity
    }
    return map_cpy
end