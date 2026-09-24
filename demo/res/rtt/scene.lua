-- A simple Render-to-Texture demo
-- Perform temporal blur using an accumulation buffer
-- This could be simplified a bit after we have multiscene, and/or render pass system

engine:clear_render_layers()

-- First, we create two render targets
-- one is out main scene texture
-- the other is our accumulation buffer

local desc0 = render_target_desc.new()
desc0.size_mode = 1 -- UI_RELATIVE
desc0.size_scale = 1.0/get_spatial(entity_find("present")).scale.x
target0 = render_target_ref.new(svc_renderer_service(), desc0) -- not local, needs to persist in script env

local desc1 = render_target_desc.new()
desc1.size_mode = 2 -- TARGET_RELATIVE
desc1.size_source = target0:id()
desc1.init_clear = true
target1 = render_target_ref.new(svc_renderer_service(), desc1) -- not local, needs to persist in script env

-- now we take the color textures created for the targets, and set them as the
-- textures of our sprites:
--   "accum" renders on target 0 on target 1
--   "present" renders target 1 on the screen

get_sprite(entity_find("accum")).spr.tex = target0:color_texture()
get_sprite(entity_find("present")).spr.tex = target1:color_texture()

target1:color_texture().nearest = true  -- use "pixelly" look for presentation

-- we create three render layers
-- the first renders the scene contents to target 0, normally
-- the second renders target 0 onto target 1, with low opacity
-- the third presents target 1 onto the screen

local cam_eid = entity_find("camera")          -- this camera renders the scene normally
local cam_tex_eid = entity_find("camera_tex")  -- and this one is used to render textures 1:1

local rl_rt      = render_layer.new()
rl_rt.follow_target = true
rl_rt.order      = 0
rl_rt.camera     = cam_eid
rl_rt.clear      = true
rl_rt.clear_r    = 0.0
rl_rt.clear_g    = 0.02
rl_rt.clear_b    = 0.03
rl_rt.target_id  = target0:id()
rl_rt.layer_mask = 1
engine:add_render_layer(rl_rt)

local rl_accum      = render_layer.new()
rl_accum.follow_target = true
rl_accum.order      = 10
rl_accum.camera     = cam_tex_eid
rl_accum.clear      = false
rl_accum.target_id  = target1:id()
rl_accum.layer_mask = 2
engine:add_render_layer(rl_accum)

local rl_present      = render_layer.new()
rl_present.follow_ui  = true
rl_present.order      = 20
rl_present.camera     = cam_tex_eid
rl_rt.clear      = false
rl_present.layer_mask = 4
engine:add_render_layer(rl_present)


-- TODO allow changing texture scale interactively (update_scale method in target_handle and service?)
--      allow changing effect strength interactively (interactive controls could use some new demo system API for UI params)
