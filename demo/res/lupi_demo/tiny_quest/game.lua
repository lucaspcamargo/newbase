-- Tiny Quest — a small JRPG vertical slice for the Lupi console (and our compat system)
-- Art: Tiny 16: Basic by Lanea Zimmerman ("Sharm"), see gfx/LICENSE.txt.

-- This is the main game controller, and handles scene change and updates
-- All game logic is within scene code


require("colors")
local SceneTitle = require("scene_title")

local curr_scene = SceneTitle:new()
local next_scene = nil

curr_frame = 0

function game_setup()
    colors_fade_in(30)
end

function update(frame)
    curr_frame = curr_frame + 1

    colors_update()

    if next_scene ~= nil then
        curr_scene = next_scene
        print("[game] scene change")
        next_scene = nil
        colors_fade_in()
    end

    if curr_scene ~= nil then
        curr_scene:update(curr_frame)
        curr_scene:draw()
        next_scene = curr_scene:get_next_scene()
    end

end


-- Perform setup at module init

game_setup()