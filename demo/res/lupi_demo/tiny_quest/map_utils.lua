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


function map_prepare(original)

    -- here we make a copy of the original map, and do all the processing we need on the copy
    -- we return the copy, and any extracted data

    local map_cpy = map_deep_copy(original)

    local map_w, map_h = map_cpy.metadata.width, map_cpy.metadata.height
    local obj_layer = map_cpy.obj.basictiles

    for row = 1, map_h do
        for column = 1, map_w do
            local tile_index = (row - 1) * map_w + column
            local tile = obj_layer[tile_index]
        end
    end

    return map_cpy
end