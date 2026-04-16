-- asteroid script: screen wrapping only

clock_update_add(function(delta)
    local sp = c_spatial()
    if not sp then return end

    local renderer_svc = svc_renderer_service()
    if not renderer_svc then return end

    local e  = renderer_svc:get_2d_extents()
    local w  = e.xspan
    local h  = e.yspan
    local nx = sp.pos.x
    local ny = sp.pos.y

    if sp.pos.x < -w/2 then nx = sp.pos.x + w
    elseif sp.pos.x > w/2 then nx = sp.pos.x - w end

    if sp.pos.y < -h/2 then ny = sp.pos.y + h
    elseif sp.pos.y > h/2 then ny = sp.pos.y - h end

    if nx ~= sp.pos.x or ny ~= sp.pos.y then
        physics2d_body_warp(eid, vec2.new(nx, ny))
    end
end)
