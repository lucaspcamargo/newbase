#include <newbase/input/overlay.hpp>
#include <newbase/engine.hpp>
#include <newbase/log.hpp>
#include <newbase/services/renderer_service.hpp>
#include <entt/entt.hpp>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_video.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <string>

using namespace nb;

static constexpr ImU32 OVERLAY_COLOR_IDLE = IM_COL32(30, 35, 42, 145);
static constexpr ImU32 OVERLAY_COLOR_ACTIVE = IM_COL32(215, 215, 215, 205);
static constexpr ImU32 OVERLAY_COLOR_OUTLINE = IM_COL32(220, 225, 230, 180);
static constexpr ImU32 OVERLAY_COLOR_SEPARATOR = IM_COL32(220, 225, 230, 150);

input_overlay::input_overlay() = default;

input_overlay::~input_overlay()
{
    shutdown();
}

bool input_overlay::init()
{
    if(_joystick)
        return true;

    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 2;
    desc.nbuttons = 8;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_SOUTH) |
                       (1u << SDL_GAMEPAD_BUTTON_EAST) |
                       (1u << SDL_GAMEPAD_BUTTON_WEST) |
                       (1u << SDL_GAMEPAD_BUTTON_NORTH) |
                       (1u << SDL_GAMEPAD_BUTTON_DPAD_UP) |
                       (1u << SDL_GAMEPAD_BUTTON_DPAD_DOWN) |
                       (1u << SDL_GAMEPAD_BUTTON_DPAD_LEFT) |
                       (1u << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) |
                     (1u << SDL_GAMEPAD_AXIS_LEFTY);
    desc.name = "newbase Touch Controller";

    _joystick_id = SDL_AttachVirtualJoystick(&desc);
    if(!_joystick_id)
    {
        log::warn("[input] overlay: cannot attach virtual joystick: %s", SDL_GetError());
        return false;
    }

    char guid_string[33] {};
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(_joystick_id), guid_string, sizeof(guid_string));
    const std::string mapping = std::string(guid_string) +
        ",newbase Touch Controller,a:b0,b:b1,x:b2,y:b3,dpup:b4,dpdown:b5,dpleft:b6,dpright:b7,leftx:a0,lefty:a1";
    if(SDL_AddGamepadMapping(mapping.c_str()) < 0)
    {
        log::warn("[input] overlay: cannot add virtual joystick mapping: %s", SDL_GetError());
        SDL_DetachVirtualJoystick(_joystick_id);
        _joystick_id = 0;
        return false;
    }

    _joystick = SDL_OpenJoystick(_joystick_id);
    if(!_joystick)
    {
        log::warn("[input] overlay: cannot open virtual joystick: %s", SDL_GetError());
        SDL_DetachVirtualJoystick(_joystick_id);
        _joystick_id = 0;
        return false;
    }
    log::info("[input] overlay: virtual joystick attached: %u", _joystick_id);
    return true;
}

void input_overlay::shutdown()
{
    reset_controls();
    if(_joystick)
    {
        SDL_CloseJoystick(_joystick);
        _joystick = nullptr;
    }
    if(_joystick_id)
    {
        SDL_DetachVirtualJoystick(_joystick_id);
        _joystick_id = 0;
    }
    _touches.clear();
}

void input_overlay::set_enabled(bool enabled)
{
    if(_enabled == enabled)
        return;
    _enabled = enabled;
    if(!_enabled)
    {
        reset_controls();
        _touches.clear();
    }
}

void input_overlay::set_dpad_mode(bool dpad)
{
    if(_dpad_mode == dpad)
        return;
    reset_controls();
    _dpad_mode = dpad;
}

input_overlay::layout input_overlay::current_layout() const
{
    ImVec2 pos = ImGui::GetMainViewport()->WorkPos;
    ImVec2 size = ImGui::GetMainViewport()->WorkSize;
    if(auto *renderer = entt::locator<renderer_service*>::value_or(nullptr))
    {
        renderer_service::extents_2d extents;
        if(renderer->get_2d_extents(extents) && extents.ui_scale > 0.0f)
        {
            pos = {extents.screen_x / extents.ui_scale, extents.screen_y / extents.ui_scale};
            size = {extents.width / extents.ui_scale, extents.height / extents.ui_scale};
        }
    }

    const float unit = std::min(size.x, size.y);
    const float button_radius = std::min(std::clamp(unit * 0.075f, 28.0f, 62.0f), unit / 4.85f);
    const float stick_radius = button_radius * 1.65f;
    const float margin = button_radius * 1.25f;
    const glm::vec2 button_origin {
        pos.x + size.x - margin - button_radius * 3.6f,
        pos.y + size.y - margin - button_radius * 3.6f
    };
    layout result;
    result.stick_center = glm::vec2{pos.x + margin + stick_radius,
                                    pos.y + size.y - margin - stick_radius};
    result.stick_radius = stick_radius;
    result.button_centers = {
        glm::vec2{button_origin.x + button_radius * 1.4f,
                  button_origin.y + button_radius * 2.8f},
        glm::vec2{button_origin.x + button_radius * 2.8f,
                  button_origin.y + button_radius * 1.4f},
        glm::vec2{button_origin.x, button_origin.y + button_radius * 1.4f},
        glm::vec2{button_origin.x + button_radius * 1.4f, button_origin.y}
    };
    result.button_radius = button_radius;
    return result;
}

void input_overlay::event(const SDL_Event &event)
{
    if(!_enabled)
        return;
    if(event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        reset_controls();
        _touches.clear();
        return;
    }
    if(event.type != SDL_EVENT_FINGER_DOWN && event.type != SDL_EVENT_FINGER_MOTION &&
       event.type != SDL_EVENT_FINGER_UP && event.type != SDL_EVENT_FINGER_CANCELED)
        return;

    SDL_Window *window = SDL_GetWindowFromID(event.tfinger.windowID);
    int width = 0, height = 0;
    if(!window || !SDL_GetWindowSize(window, &width, &height) || width <= 0 || height <= 0)
        return;
    const glm::vec2 position {event.tfinger.x * width, event.tfinger.y * height};
    if(event.type == SDL_EVENT_FINGER_DOWN)
        begin_touch(event.tfinger.fingerID, position);
    else if(event.type == SDL_EVENT_FINGER_MOTION)
        update_touch(event.tfinger.fingerID, position);
    else
        end_touch(event.tfinger.fingerID);
}

void input_overlay::begin_touch(SDL_FingerID finger, glm::vec2 position)
{
    const auto layout = current_layout();
    if(position.x < layout.stick_center.x + layout.stick_radius * 1.35f)
    {
        _touches[finger] = control::LEFT_STICK;
        if(_dpad_mode)
            update_dpad(position, layout);
        else
            update_stick(position, layout);
        return;
    }
    for(size_t i = 0; i < layout.button_centers.size(); ++i)
    {
        if(glm::length(position - layout.button_centers[i]) <= layout.button_radius * 1.25f)
        {
            const auto button = static_cast<control>(static_cast<unsigned char>(control::SOUTH) + i);
            _touches[finger] = button;
            set_button(button, true);
            return;
        }
    }
}

void input_overlay::update_touch(SDL_FingerID finger, glm::vec2 position)
{
    auto it = _touches.find(finger);
    if(it != _touches.end() && it->second == control::LEFT_STICK)
    {
        if(_dpad_mode)
            update_dpad(position, current_layout());
        else
            update_stick(position, current_layout());
    }
}

void input_overlay::end_touch(SDL_FingerID finger)
{
    auto it = _touches.find(finger);
    if(it == _touches.end())
        return;
    if(it->second == control::LEFT_STICK)
    {
        _stick_position = {0.0f, 0.0f};
        if(_dpad_mode)
            set_dpad_direction({0.0f, 0.0f});
        if(_joystick)
        {
            SDL_SetJoystickVirtualAxis(_joystick, SDL_GAMEPAD_AXIS_LEFTX, 0);
            SDL_SetJoystickVirtualAxis(_joystick, SDL_GAMEPAD_AXIS_LEFTY, 0);
        }
    }
    else
        set_button(it->second, false);
    _touches.erase(it);
}

void input_overlay::update_dpad(glm::vec2 position, const layout &layout)
{
    glm::vec2 delta = position - layout.stick_center;
    if(glm::length(delta) < layout.stick_radius * 0.25f)
    {
        set_dpad_direction({0.0f, 0.0f});
        return;
    }
    const float angle = std::atan2(delta.y, delta.x);
    set_dpad_direction({std::cos(angle), std::sin(angle)});
}

void input_overlay::set_dpad_direction(glm::vec2 direction)
{
    const bool right = direction.x > 0.382683f;
    const bool left = direction.x < -0.382683f;
    const bool down = direction.y > 0.382683f;
    const bool up = direction.y < -0.382683f;
    const std::array<bool, 4> next {up, down, left, right};
    for(size_t i = 0; i < next.size(); ++i)
    {
        if(_dpad[i] == next[i])
            continue;
        _dpad[i] = next[i];
        if(_joystick)
            SDL_SetJoystickVirtualButton(_joystick, 4 + static_cast<int>(i), next[i]);
    }
}

void input_overlay::update_stick(glm::vec2 position, const layout &layout)
{
    glm::vec2 delta = position - layout.stick_center;
    const float distance = glm::length(delta);
    if(distance > layout.stick_radius)
        delta *= layout.stick_radius / distance;
    _stick_position = delta / layout.stick_radius;
    if(glm::length(_stick_position) < 0.15f)
        _stick_position = {0.0f, 0.0f};
    if(_joystick)
    {
        SDL_SetJoystickVirtualAxis(_joystick, SDL_GAMEPAD_AXIS_LEFTX,
            static_cast<Sint16>(_stick_position.x * 32767.0f));
        SDL_SetJoystickVirtualAxis(_joystick, SDL_GAMEPAD_AXIS_LEFTY,
            static_cast<Sint16>(_stick_position.y * 32767.0f));
    }
}

void input_overlay::set_button(control control, bool pressed)
{
    const size_t index = static_cast<size_t>(static_cast<unsigned char>(control) -
                                              static_cast<unsigned char>(control::SOUTH));
    if(index >= _buttons.size())
        return;
    _buttons[index] = pressed;
    if(_joystick)
        SDL_SetJoystickVirtualButton(_joystick,
            static_cast<int>(SDL_GAMEPAD_BUTTON_SOUTH) + static_cast<int>(index), pressed);
}

void input_overlay::reset_controls()
{
    _stick_position = {0.0f, 0.0f};
    for(size_t i = 0; i < _buttons.size(); ++i)
        set_button(static_cast<control>(static_cast<unsigned char>(control::SOUTH) + i), false);
    set_dpad_direction({0.0f, 0.0f});
    if(_joystick)
    {
        SDL_SetJoystickVirtualAxis(_joystick, SDL_GAMEPAD_AXIS_LEFTX, 0);
        SDL_SetJoystickVirtualAxis(_joystick, SDL_GAMEPAD_AXIS_LEFTY, 0);
    }
}

void input_overlay::draw() const
{
    if(!_visible || engine::instance().is_paused())
        return;
    const auto layout = current_layout();
    ImDrawList *draw_list = ImGui::GetForegroundDrawList();
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImVec2 clip_min = viewport->WorkPos;
    ImVec2 clip_max = {clip_min.x + viewport->WorkSize.x, clip_min.y + viewport->WorkSize.y};
    if(auto *renderer = entt::locator<renderer_service*>::value_or(nullptr))
    {
        renderer_service::extents_2d extents;
        if(renderer->get_2d_extents(extents) && extents.ui_scale > 0.0f)
        {
            clip_min = {extents.screen_x / extents.ui_scale, extents.screen_y / extents.ui_scale};
            clip_max = {clip_min.x + extents.width / extents.ui_scale,
                        clip_min.y + extents.height / extents.ui_scale};
        }
    }
    draw_list->PushClipRect(clip_min, clip_max, true);
    const ImU32 idle = OVERLAY_COLOR_IDLE;
    const ImU32 active = OVERLAY_COLOR_ACTIVE;
    if(_dpad_mode)
    {
        draw_list->AddCircleFilled({layout.stick_center.x, layout.stick_center.y}, layout.stick_radius, idle);
        constexpr float pi = 3.14159265359f;
        const float quadrant_centers[] = {-pi * 0.5f, 0.0f, pi * 0.5f, pi};
        const bool quadrant_active[] = {_dpad[0], _dpad[3], _dpad[1], _dpad[2]};
        for(size_t i = 0; i < 4; ++i)
        {
            if(!quadrant_active[i])
                continue;
            draw_list->PathClear();
            draw_list->PathLineTo({layout.stick_center.x, layout.stick_center.y});
            draw_list->PathArcTo({layout.stick_center.x, layout.stick_center.y},
                layout.stick_radius, quadrant_centers[i] - pi * 0.25f,
                quadrant_centers[i] + pi * 0.25f, 12);
            draw_list->PathFillConvex(active);
        }
        draw_list->AddCircle({layout.stick_center.x, layout.stick_center.y}, layout.stick_radius,
            OVERLAY_COLOR_OUTLINE, 32, 2.0f);
        for(float angle : {-pi * 0.25f, pi * 0.25f, pi * 0.75f, pi * 1.25f})
        {
            const ImVec2 edge {
                layout.stick_center.x + std::cos(angle) * layout.stick_radius,
                layout.stick_center.y + std::sin(angle) * layout.stick_radius
            };
            draw_list->AddLine({layout.stick_center.x, layout.stick_center.y}, edge,
                OVERLAY_COLOR_SEPARATOR, 1.0f);
        }
    }
    else
    {
        draw_list->AddCircleFilled({layout.stick_center.x, layout.stick_center.y}, layout.stick_radius, idle);
        draw_list->AddCircle({layout.stick_center.x, layout.stick_center.y}, layout.stick_radius,
            OVERLAY_COLOR_OUTLINE, 32, 2.0f);
        const ImVec2 knob {layout.stick_center.x + _stick_position.x * layout.stick_radius,
                           layout.stick_center.y + _stick_position.y * layout.stick_radius};
        draw_list->AddCircleFilled(knob, layout.stick_radius * 0.48f, active);
    }
    for(size_t i = 0; i < layout.button_centers.size(); ++i)
    {
        const auto &center = layout.button_centers[i];
        draw_list->AddCircleFilled({center.x, center.y}, layout.button_radius,
            _buttons[i] ? active : idle);
        draw_list->AddCircle({center.x, center.y}, layout.button_radius,
            OVERLAY_COLOR_OUTLINE, 24, 2.0f);
        static constexpr const char *labels[] = {"A", "B", "X", "Y"};
        const ImVec2 label_size = ImGui::CalcTextSize(labels[i]);
        draw_list->AddText({center.x - label_size.x * 0.5f,
                            center.y - label_size.y * 0.5f},
            OVERLAY_COLOR_OUTLINE, labels[i]);
    }
    draw_list->PopClipRect();
}
