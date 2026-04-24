-- fast_rodent: Sonic-style quad-mode physics
-- Coordinate system: x=right, y=down.
-- angle=0: flat floor. Increases clockwise.
--   0     → floor      (mode 0)
--   π/2   → left wall  (mode 3, gdir=left)
--   π     → ceiling    (mode 2)
--   3π/2  → right wall (mode 1, gdir=right)

local TAU = 2 * math.pi
local PI  = math.pi

-- ── unit conversions (Sonic px/frame @ 60 fps → px/s, px/s²) ─────────────────
local function spd(v) return v * 60   end
local function acc(v) return v * 3600 end

-- ── physics constants ─────────────────────────────────────────────────────────
local TOP_SPEED = spd(6)
local ACCEL     = acc(0.046875)
local DECEL     = acc(0.5)
local FRICTION  = acc(0.046875)
local GRAVITY   = acc(0.21875)
local JUMP_VEL  = spd(6.5)
local JUMP_CUT  = -spd(4)          -- max upward speed when jump released early
local SLOPE     = acc(0.125)       -- gravity projected onto surface (slope factor)

local MIN_WALL_SPEED = spd(2.5)    -- minimum gSpeed to stick to walls
local MIN_CEIL_SPEED = spd(5.0)    -- minimum gSpeed to stick to ceiling

local TILE_MASK = 1                -- Box2D category bits for tile colliders

-- ── character geometry ────────────────────────────────────────────────────────
local W2           = 9   -- half-width (feet sensor spread)
local H2           = 20  -- half-height (center to feet/head)
local GROUND_REACH = 16  -- sensor extension beyond feet
local CEIL_REACH   = 16  -- sensor extension beyond head
local WALL_REACH   = 8   -- sensor extension beyond sides

-- ── map / camera ─────────────────────────────────────────────────────────────
local MAP_W  = 60 * 70
local MAP_H  = 20 * 70
local VIEW_W = 1920
local VIEW_H = 1080
local SPAWN_X, SPAWN_Y = 490, 100

-- ── helpers ───────────────────────────────────────────────────────────────────
local function sign(v) return v > 0 and 1 or (v < 0 and -1 or 0) end
local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

-- Surface angle from a Box2D raycast normal (y-down space, result in [0, 2π)).
-- Mapping: (0,-1)→0 (floor), (1,0)→π/2 (left wall), (0,1)→π (ceiling), (-1,0)→3π/2 (right wall).
local function normal_to_angle(nx, ny)
    local a = math.atan2(nx, -ny)
    return a < 0 and a + TAU or a
end

-- From surface angle, derive:
--   tangent (tx,ty) = direction of positive gSpeed along the surface
--   gdir   (gx,gy) = "gravity" direction into the surface (perpendicular, inward)
local function surface_vectors(a)
    return  math.cos(a),  math.sin(a),   -- tangent
           -math.sin(a),  math.cos(a)    -- gdir
end

-- Quad mode from surface angle.
local function angle_to_mode(a)
    if     a < PI/4 or a >= 7*PI/4 then return 0  -- floor
    elseif a < 3*PI/4               then return 3  -- left wall  (angle ≈ π/2)
    elseif a < 5*PI/4               then return 2  -- ceiling    (angle ≈ π)
    else                                 return 1  -- right wall (angle ≈ 3π/2)
    end
end

-- ── sensors ───────────────────────────────────────────────────────────────────

-- Ground sensors A (left) and B (right) relative to the surface tangent.
-- Casts H2+GROUND_REACH in the gdir direction from each sensor origin.
-- Returns dist_to_feet (<0 = overlapping), surface_angle — or nil, 0.
local function sense_ground(cx, cy, a)
    local tx, ty, gx, gy = surface_vectors(a)
    local cast = H2 + GROUND_REACH

    local function cast_one(ox, oy)
        local r = physics2d_raycast(cx + ox, cy + oy,
                                    cx + ox + gx*cast, cy + oy + gy*cast,
                                    TILE_MASK)
        if r.x < 0 then return nil, 0 end
        return r.x * cast - H2, normal_to_angle(r.y, r.z)
    end

    local da, aa = cast_one(-tx*W2, -ty*W2)  -- A: left of tangent
    local db, ab = cast_one( tx*W2,  ty*W2)  -- B: right of tangent

    if not da and not db then return nil, 0 end
    if not da             then return db, ab end
    if not db             then return da, aa end
    if da <= db then return da, aa else return db, ab end
end

-- Ceiling sensors C (left) and D (right); cast opposite to gdir.
-- Returns closest dist_to_head (<0 = overlapping), or nil.
local function sense_ceiling(cx, cy, a)
    local tx, ty, gx, gy = surface_vectors(a)
    local cast = H2 + CEIL_REACH

    local function cast_one(ox, oy)
        local r = physics2d_raycast(cx + ox, cy + oy,
                                    cx + ox - gx*cast, cy + oy - gy*cast,
                                    TILE_MASK)
        if r.x < 0 then return nil end
        return r.x * cast - H2
    end

    local dc = cast_one(-tx*W2, -ty*W2)
    local dd = cast_one( tx*W2,  ty*W2)
    if not dc and not dd then return nil end
    if not dc then return dd end
    if not dd then return dc end
    return math.min(dc, dd)
end

-- Horizontal wall sensor (always axis-aligned, not rotated with mode).
-- dir: +1=right, -1=left. Cast from two body heights.
-- Returns dist from side (<0 = overlap), or nil.
local function sense_wall(cx, cy, dir)
    local cast = W2 + WALL_REACH
    local r1 = physics2d_raycast(cx, cy - H2*0.5, cx + dir*cast, cy - H2*0.5, TILE_MASK)
    local r2 = physics2d_raycast(cx, cy + H2*0.5, cx + dir*cast, cy + H2*0.5, TILE_MASK)
    local function hit(r) return r.x >= 0 and r.x * cast - W2 or nil end
    local d1, d2 = hit(r1), hit(r2)
    if not d1 and not d2 then return nil end
    if not d1 then return d2 end
    if not d2 then return d1 end
    return math.min(d1, d2)
end

-- ── player state ──────────────────────────────────────────────────────────────
local sp0  = c_spatial()
local px   = sp0 and sp0.pos.x or SPAWN_X
local py   = sp0 and sp0.pos.y or SPAWN_Y

local gSpeed   = 0      -- ground speed (px/s along surface tangent)
local xSpeed   = 0      -- air x velocity
local ySpeed   = 0      -- air y velocity
local angle    = 0      -- surface angle (radians, 0 = flat floor)
local mode     = 0      -- quad mode: 0=floor 1=right-wall 2=ceiling 3=left-wall
local grounded = false
local jumping  = false
local facing   = 1      -- 1=right, -1=left

local dir_hs  = hs("dir")
local jump_hs = hs("btn_south")

local function respawn()
    px, py   = SPAWN_X, SPAWN_Y
    gSpeed   = 0; xSpeed = 0; ySpeed = 0
    angle    = 0; mode   = 0
    grounded = false; jumping = false; facing = 1
    physics2d_character_warp(eid, vec2.new(px, py))
end

-- ── main update ───────────────────────────────────────────────────────────────
local update_handle = clock_update_add(function(dt)

    if px < 0 or px > MAP_W or py < 0 or py > MAP_H then
        respawn(); return
    end

    local dir     = input_action_direction(dir_hs)
    local inp_x   = dir.x
    local do_jump = input_action_was_pressed(jump_hs)

    -- Horizontal wall sensors.
    -- Skipped in wall-running modes (1, 3) to avoid false collision with the
    -- surface the character is currently sticking to.
    if mode == 0 or mode == 2 or not grounded then
        local wd_r = sense_wall(px, py,  1)
        local wd_l = sense_wall(px, py, -1)
        if wd_r and wd_r < 0 then
            px = px + wd_r  -- push left
            if grounded and gSpeed > 0 then gSpeed = 0 end
            if not grounded and xSpeed > 0 then xSpeed = 0 end
        end
        if wd_l and wd_l < 0 then
            px = px - wd_l  -- push right (wd_l < 0, so -= negative = +)
            if grounded and gSpeed < 0 then gSpeed = 0 end
            if not grounded and xSpeed < 0 then xSpeed = 0 end
        end
    end

    if grounded then
        -- ── ground physics ────────────────────────────────────────────────────
        local tx, ty, gx, gy = surface_vectors(angle)

        if inp_x > 0 then
            facing = 1
            if gSpeed < 0 then
                gSpeed = gSpeed + DECEL * dt
            else
                gSpeed = math.min(gSpeed + ACCEL * dt, TOP_SPEED)
            end
        elseif inp_x < 0 then
            facing = -1
            if gSpeed > 0 then
                gSpeed = gSpeed - DECEL * dt
            else
                gSpeed = math.max(gSpeed - ACCEL * dt, -TOP_SPEED)
            end
        else
            local f = FRICTION * dt
            if math.abs(gSpeed) <= f then
                gSpeed = 0
            else
                gSpeed = gSpeed - sign(gSpeed) * f
            end
        end

        -- Slope factor: component of world gravity along the surface tangent.
        -- gSpeed += GRAVITY * sin(angle) * dt (positive = downhill in tangent direction).
        gSpeed = gSpeed + SLOPE * math.sin(angle) * dt
        gSpeed = clamp(gSpeed, -TOP_SPEED, TOP_SPEED)

        -- World velocity and position
        xSpeed = gSpeed * tx
        ySpeed = gSpeed * ty
        px = px + xSpeed * dt
        py = py + ySpeed * dt

        if do_jump then
            -- Launch perpendicular to surface (opposite gdir), carry surface momentum
            xSpeed = xSpeed - gx * JUMP_VEL
            ySpeed = ySpeed - gy * JUMP_VEL
            grounded = false; jumping = true
            angle = 0; mode = 0
        else
            -- Ground snap: sense and follow the surface
            local dist, new_angle = sense_ground(px, py, angle)
            if dist and dist < GROUND_REACH then
                -- Move along old gdir by dist to place feet on new surface
                px = px + gx * dist
                py = py + gy * dist
                angle = new_angle
                mode  = angle_to_mode(angle)

                -- Fall-off: walls and ceiling require minimum speed
                if (mode == 1 or mode == 3) and math.abs(gSpeed) < MIN_WALL_SPEED then
                    grounded = false; angle = 0; mode = 0
                elseif mode == 2 and math.abs(gSpeed) < MIN_CEIL_SPEED then
                    grounded = false; angle = 0; mode = 0
                end
            else
                -- Ran off an edge
                grounded = false; angle = 0; mode = 0
            end
        end

    else
        -- ── air physics ───────────────────────────────────────────────────────

        -- Variable jump height: cut upward speed when button released early
        if jumping and not input_action_is_pressed(jump_hs) and ySpeed < JUMP_CUT then
            ySpeed = JUMP_CUT
        end

        -- Air x control (same acceleration as ground; Sonic doesn't reduce it in air)
        if inp_x > 0 then
            xSpeed = math.min(xSpeed + ACCEL * dt, TOP_SPEED)
            facing = 1
        elseif inp_x < 0 then
            xSpeed = math.max(xSpeed - ACCEL * dt, -TOP_SPEED)
            facing = -1
        end

        ySpeed = ySpeed + GRAVITY * dt
        px = px + xSpeed * dt
        py = py + ySpeed * dt

        -- Ceiling sensor (only relevant when moving upward)
        if ySpeed < 0 then
            local cd = sense_ceiling(px, py, 0)
            if cd and cd < 0 then
                py = py - cd  -- push down (cd < 0, so py increases = moves down)
                ySpeed = 0
                jumping = false
            end
        end

        -- Ground landing (only when moving downward or at apex)
        if ySpeed >= 0 then
            local dist, new_angle = sense_ground(px, py, 0)
            if dist and dist <= 0 then
                py = py + dist  -- snap up (dist < 0, so py decreases)
                angle    = new_angle
                mode     = angle_to_mode(angle)
                -- Project air velocity onto landing surface tangent
                local tx, ty = math.cos(angle), math.sin(angle)
                gSpeed   = xSpeed * tx + ySpeed * ty
                grounded = true; jumping = false
            end
        end
    end

    -- Apply position to entity
    physics2d_character_warp(eid, vec2.new(px, py))

    -- Camera follow, clamped to map bounds
    local cam_x = clamp(px, VIEW_W * 0.5, MAP_W - VIEW_W * 0.5)
    local cam_y = clamp(py, VIEW_H * 0.5, MAP_H - VIEW_H * 0.5)
    render_simple_cam_2d_setup(cam_x, cam_y, VIEW_W, VIEW_H)
end)

script_on_destroy(function()
    clock_update_remove(update_handle)
end)
