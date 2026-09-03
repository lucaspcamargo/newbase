// Simple fluid simulation experiment based on Jos Stam's famous paper, "Real-Time Fluid Dynamics for Games"
// Simple, CPU-based, naive implementation, not yet properly integrated into ECS or rendering

#include <newbase/fluid_2d/fluid_2d.hpp>
#include <newbase/clock/clock.hpp>
#include <newbase/utility/glm.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/log.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <entt/entt.hpp>
#include <imgui.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace nb;
using entt::operator""_hs;

namespace
{
constexpr int GRID_WIDTH = 256;
constexpr int GRID_HEIGHT = 128;
constexpr int DEFAULT_SOLVER_ITERATIONS = 12;
constexpr float DEFAULT_VISCOSITY = 0.0001f;
constexpr float DEFAULT_DIFFUSION = 0.0001f;
constexpr float DEFAULT_FORCE = 8.0f;
constexpr float DEFAULT_BUOYANCY = 1.0f;
constexpr int INJECTION_RADIUS = 5;
constexpr ImU32 FLUID_GRID_COLOR = IM_COL32(100, 120, 145, 90);
constexpr ImU32 FLUID_VELOCITY_COLOR = IM_COL32(150, 210, 235, 170);

struct viewport_rect
{
    ImVec2 pos;
    ImVec2 size;
};

viewport_rect get_viewport()
{
    const ImGuiViewport *main = ImGui::GetMainViewport();
    viewport_rect result {main->WorkPos, main->WorkSize};
    if(auto *renderer = entt::locator<renderer_service*>::value_or(nullptr))
    {
        renderer_service::extents_2d extents;
        if(renderer->get_2d_extents(extents) && extents.ui_scale > 0.0f)
        {
            result.pos = {extents.screen_x / extents.ui_scale, extents.screen_y / extents.ui_scale};
            result.size = {extents.width / extents.ui_scale, extents.height / extents.ui_scale};
        }
    }
    return result;
}

float clamp01(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}
}

struct nb::fluid_2d_p
{
    std::vector<float> density {GRID_WIDTH * GRID_HEIGHT, 0.0f};
    std::vector<float> density_previous {GRID_WIDTH * GRID_HEIGHT, 0.0f};
    std::vector<float> velocity_x {GRID_WIDTH * GRID_HEIGHT, 0.0f};
    std::vector<float> velocity_y {GRID_WIDTH * GRID_HEIGHT, 0.0f};
    std::vector<float> velocity_x_previous {GRID_WIDTH * GRID_HEIGHT, 0.0f};
    std::vector<float> velocity_y_previous {GRID_WIDTH * GRID_HEIGHT, 0.0f};

    float viscosity {DEFAULT_VISCOSITY};
    float diffusion {DEFAULT_DIFFUSION};
    float force {DEFAULT_FORCE};
    float buoyancy {DEFAULT_BUOYANCY};
    int solver_iterations {DEFAULT_SOLVER_ITERATIONS};
    bool draw_grid {false};
    bool draw_velocity {false};
    bool dragging {false};
    renderer_service::texture_handle smoke_texture {nullptr};
    SDL_Surface *smoke_surface {nullptr};
    static int index(int x, int y)
    {
        return x + (GRID_WIDTH + 2) * y;
    }

    void resize_fields()
    {
        const size_t size = static_cast<size_t>(GRID_WIDTH + 2) * (GRID_HEIGHT + 2);
        density.resize(size, 0.0f);
        density_previous.resize(size, 0.0f);
        velocity_x.resize(size, 0.0f);
        velocity_y.resize(size, 0.0f);
        velocity_x_previous.resize(size, 0.0f);
        velocity_y_previous.resize(size, 0.0f);
    }

    void clear()
    {
        std::fill(density.begin(), density.end(), 0.0f);
        std::fill(density_previous.begin(), density_previous.end(), 0.0f);
        std::fill(velocity_x.begin(), velocity_x.end(), 0.0f);
        std::fill(velocity_y.begin(), velocity_y.end(), 0.0f);
        std::fill(velocity_x_previous.begin(), velocity_x_previous.end(), 0.0f);
        std::fill(velocity_y_previous.begin(), velocity_y_previous.end(), 0.0f);
    }
};

static void set_boundary(int type, std::vector<float> &field)
{
    for(int x = 1; x <= GRID_WIDTH; ++x)
    {
        field[fluid_2d_p::index(x, 0)] = type == 2 ? -field[fluid_2d_p::index(x, 1)] : field[fluid_2d_p::index(x, 1)];
        field[fluid_2d_p::index(x, GRID_HEIGHT + 1)] = type == 2 ? -field[fluid_2d_p::index(x, GRID_HEIGHT)] : field[fluid_2d_p::index(x, GRID_HEIGHT)];
    }
    for(int y = 1; y <= GRID_HEIGHT; ++y)
    {
        field[fluid_2d_p::index(0, y)] = type == 1 ? -field[fluid_2d_p::index(1, y)] : field[fluid_2d_p::index(1, y)];
        field[fluid_2d_p::index(GRID_WIDTH + 1, y)] = type == 1 ? -field[fluid_2d_p::index(GRID_WIDTH, y)] : field[fluid_2d_p::index(GRID_WIDTH, y)];
    }
    field[fluid_2d_p::index(0, 0)] = 0.5f * (field[fluid_2d_p::index(1, 0)] + field[fluid_2d_p::index(0, 1)]);
    field[fluid_2d_p::index(0, GRID_HEIGHT + 1)] = 0.5f * (field[fluid_2d_p::index(1, GRID_HEIGHT + 1)] + field[fluid_2d_p::index(0, GRID_HEIGHT)]);
    field[fluid_2d_p::index(GRID_WIDTH + 1, 0)] = 0.5f * (field[fluid_2d_p::index(GRID_WIDTH, 0)] + field[fluid_2d_p::index(GRID_WIDTH + 1, 1)]);
    field[fluid_2d_p::index(GRID_WIDTH + 1, GRID_HEIGHT + 1)] = 0.5f * (field[fluid_2d_p::index(GRID_WIDTH, GRID_HEIGHT + 1)] + field[fluid_2d_p::index(GRID_WIDTH + 1, GRID_HEIGHT)]);
}

static void add_source(std::vector<float> &field, const std::vector<float> &source, float dt)
{
    for(size_t i = 0; i < field.size(); ++i)
        field[i] += dt * source[i];
}

static void diffuse(int type, std::vector<float> &field, const std::vector<float> &source,
                    float amount, float dt, int iterations)
{
    const float coefficient = dt * amount * GRID_WIDTH * GRID_HEIGHT;
    for(int iteration = 0; iteration < iterations; ++iteration)
        for(int y = 1; y <= GRID_HEIGHT; ++y)
            for(int x = 1; x <= GRID_WIDTH; ++x)
                field[fluid_2d_p::index(x, y)] =
                    (source[fluid_2d_p::index(x, y)] + coefficient *
                        (field[fluid_2d_p::index(x - 1, y)] + field[fluid_2d_p::index(x + 1, y)] +
                         field[fluid_2d_p::index(x, y - 1)] + field[fluid_2d_p::index(x, y + 1)])) /
                    (1.0f + 4.0f * coefficient);
    set_boundary(type, field);
}

static float sample(const std::vector<float> &field, float x, float y)
{
    x = std::max(0.5f, std::min(static_cast<float>(GRID_WIDTH) + 0.5f, x));
    y = std::max(0.5f, std::min(static_cast<float>(GRID_HEIGHT) + 0.5f, y));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const float sx = x - static_cast<float>(x0);
    const float sy = y - static_cast<float>(y0);
    return (1.0f - sx) * ((1.0f - sy) * field[fluid_2d_p::index(x0, y0)] + sy * field[fluid_2d_p::index(x0, y0 + 1)]) +
           sx * ((1.0f - sy) * field[fluid_2d_p::index(x0 + 1, y0)] + sy * field[fluid_2d_p::index(x0 + 1, y0 + 1)]);
}

static void advect(int type, std::vector<float> &field, const std::vector<float> &source,
                   const std::vector<float> &velocity_x, const std::vector<float> &velocity_y, float dt)
{
    const float scale_x = dt * GRID_WIDTH;
    const float scale_y = dt * GRID_HEIGHT;
    for(int y = 1; y <= GRID_HEIGHT; ++y)
        for(int x = 1; x <= GRID_WIDTH; ++x)
        {
            const float source_x = static_cast<float>(x) - scale_x * velocity_x[fluid_2d_p::index(x, y)];
            const float source_y = static_cast<float>(y) - scale_y * velocity_y[fluid_2d_p::index(x, y)];
            field[fluid_2d_p::index(x, y)] = sample(source, source_x, source_y);
        }
    set_boundary(type, field);
}

static void project(std::vector<float> &velocity_x, std::vector<float> &velocity_y,
                    std::vector<float> &pressure, std::vector<float> &divergence,
                    int iterations)
{
    for(int y = 1; y <= GRID_HEIGHT; ++y)
        for(int x = 1; x <= GRID_WIDTH; ++x)
        {
            divergence[fluid_2d_p::index(x, y)] = -0.5f *
                (velocity_x[fluid_2d_p::index(x + 1, y)] - velocity_x[fluid_2d_p::index(x - 1, y)] +
                 velocity_y[fluid_2d_p::index(x, y + 1)] - velocity_y[fluid_2d_p::index(x, y - 1)]) /
                static_cast<float>(std::max(GRID_WIDTH, GRID_HEIGHT));
            pressure[fluid_2d_p::index(x, y)] = 0.0f;
        }
    set_boundary(0, divergence);
    set_boundary(0, pressure);
    for(int iteration = 0; iteration < iterations; ++iteration)
        for(int y = 1; y <= GRID_HEIGHT; ++y)
            for(int x = 1; x <= GRID_WIDTH; ++x)
                pressure[fluid_2d_p::index(x, y)] = (divergence[fluid_2d_p::index(x, y)] +
                    pressure[fluid_2d_p::index(x - 1, y)] + pressure[fluid_2d_p::index(x + 1, y)] +
                    pressure[fluid_2d_p::index(x, y - 1)] + pressure[fluid_2d_p::index(x, y + 1)]) * 0.25f;
    set_boundary(0, pressure);
    for(int y = 1; y <= GRID_HEIGHT; ++y)
        for(int x = 1; x <= GRID_WIDTH; ++x)
        {
            velocity_x[fluid_2d_p::index(x, y)] -= 0.5f *
                (pressure[fluid_2d_p::index(x + 1, y)] - pressure[fluid_2d_p::index(x - 1, y)]);
            velocity_y[fluid_2d_p::index(x, y)] -= 0.5f *
                (pressure[fluid_2d_p::index(x, y + 1)] - pressure[fluid_2d_p::index(x, y - 1)]);
        }
    set_boundary(1, velocity_x);
    set_boundary(2, velocity_y);
}

fluid_2d::fluid_2d() : _d(new fluid_2d_p)
{
    _d->resize_fields();
}

fluid_2d::~fluid_2d()
{
    if(auto *ui = entt::locator<ui_manager*>::value_or(nullptr))
        ui->unregister_overlay("fluid_2d");
    if(auto *renderer = entt::locator<renderer_service*>::value_or(nullptr))
        if(_d->smoke_texture)
            renderer->destroy_texture(_d->smoke_texture);
    if(_d->smoke_surface)
        SDL_DestroySurface(_d->smoke_surface);
    delete _d;
}

bool fluid_2d::init(ryml::ConstNodeRef cfg)
{
    log::info("[fluid_2d] init");
    
    if(!cfg.invalid() && !cfg.empty())
    {
        if(cfg.has_child("viscosity")) cfg["viscosity"] >> _d->viscosity;
        if(cfg.has_child("diffusion")) cfg["diffusion"] >> _d->diffusion;
        if(cfg.has_child("force")) cfg["force"] >> _d->force;
        if(cfg.has_child("buoyancy")) cfg["buoyancy"] >> _d->buoyancy;
        if(cfg.has_child("iterations")) cfg["iterations"] >> _d->solver_iterations;
        if(cfg.has_child("draw_grid")) cfg["draw_grid"] >> _d->draw_grid;
        if(cfg.has_child("draw_velocity")) cfg["draw_velocity"] >> _d->draw_velocity;
    }
    _d->solver_iterations = std::max(1, _d->solver_iterations);
    if(auto *renderer = entt::locator<renderer_service*>::value_or(nullptr))
    {
        _d->smoke_surface = SDL_CreateSurface(GRID_WIDTH, GRID_HEIGHT, SDL_PIXELFORMAT_RGBA32);
        _d->smoke_texture = renderer->create_texture(GRID_WIDTH, GRID_HEIGHT);
        if(!_d->smoke_surface || !_d->smoke_texture)
        {
            if(_d->smoke_texture)
                renderer->destroy_texture(_d->smoke_texture);
            _d->smoke_texture = nullptr;
            if(_d->smoke_surface)
                SDL_DestroySurface(_d->smoke_surface);
            _d->smoke_surface = nullptr;
        }
    }
    if(auto *ui = entt::locator<ui_manager*>::value_or(nullptr))
        ui->register_overlay("fluid_2d", [this]() {
            const viewport_rect viewport = get_viewport();
            if(viewport.size.x <= 0.0f || viewport.size.y <= 0.0f)
                return;
            ImDrawList *draw = ImGui::GetForegroundDrawList();
            draw->PushClipRect(viewport.pos,
                {viewport.pos.x + viewport.size.x, viewport.pos.y + viewport.size.y}, true);
            const float cell_width = viewport.size.x / GRID_WIDTH;
            const float cell_height = viewport.size.y / GRID_HEIGHT;

            if(_d->smoke_texture && _d->smoke_surface)
            {
                uint8_t *pixels = static_cast<uint8_t*>(_d->smoke_surface->pixels);
                for(int y = 1; y <= GRID_HEIGHT; ++y)
                    for(int x = 1; x <= GRID_WIDTH; ++x)
                    {
                        const float value = clamp01(_d->density[fluid_2d_p::index(x, y)] * 0.08f);
                        uint8_t *pixel = pixels + (y - 1) * _d->smoke_surface->pitch + (x - 1) * 4;
                        pixel[0] = static_cast<uint8_t>(20.0f + value * 100.0f);
                        pixel[1] = static_cast<uint8_t>(40.0f + value * 180.0f);
                        pixel[2] = static_cast<uint8_t>(70.0f + value * 185.0f);
                        pixel[3] = static_cast<uint8_t>(value * 210.0f);
                    }
                if(auto *renderer = entt::locator<renderer_service*>::value_or(nullptr))
                    renderer->update_texture(_d->smoke_texture, _d->smoke_surface->pixels,
                        _d->smoke_surface->pitch);
                draw->AddImage(_d->smoke_texture, viewport.pos,
                    {viewport.pos.x + viewport.size.x, viewport.pos.y + viewport.size.y});
            }

            if(_d->draw_grid)
            {
                for(int x = 0; x <= GRID_WIDTH; ++x)
                {
                    const float screen_x = viewport.pos.x + x * cell_width;
                    draw->AddLine({screen_x, viewport.pos.y},
                        {screen_x, viewport.pos.y + viewport.size.y}, FLUID_GRID_COLOR);
                }
                for(int y = 0; y <= GRID_HEIGHT; ++y)
                {
                    const float screen_y = viewport.pos.y + y * cell_height;
                    draw->AddLine({viewport.pos.x, screen_y},
                        {viewport.pos.x + viewport.size.x, screen_y}, FLUID_GRID_COLOR);
                }
            }

            for(int y = 1; y <= GRID_HEIGHT; ++y)
                for(int x = 1; x <= GRID_WIDTH; ++x)
                {
                    if(_d->draw_velocity)
                    {
                        const glm::vec2 center {
                            viewport.pos.x + (x - 0.5f) * cell_width,
                            viewport.pos.y + (y - 0.5f) * cell_height
                        };
                        const glm::vec2 velocity {
                            _d->velocity_x[fluid_2d_p::index(x, y)],
                            _d->velocity_y[fluid_2d_p::index(x, y)]
                        };
                        const float velocity_length = glm::length(velocity);
                        if(velocity_length > 0.01f)
                        {
                            const glm::vec2 direction = velocity / velocity_length;
                            const float vector_length = std::min(velocity_length * 2.0f,
                                std::min(cell_width, cell_height) * 0.4f);
                            const ImVec2 tip {
                                center.x + direction.x * vector_length,
                                center.y + direction.y * vector_length
                            };
                            draw->AddLine({center.x, center.y}, tip,
                                FLUID_VELOCITY_COLOR, 1.5f);
                        }
                    }
                }
            draw->PopClipRect();
        });
    return true;
}

bool fluid_2d::step(step_phase phase)
{
    if(phase != step_phase::GENERAL_UPDATE)
        return true;
    const float dt = entt::locator<clock*>::has_value()
        ? std::min(entt::locator<clock*>::value()->get_dt(), 0.1f) : 1.0f / 60.0f;
    if(_d->buoyancy != 0.0f)
        for(int y = 1; y <= GRID_HEIGHT; ++y)
            for(int x = 1; x <= GRID_WIDTH; ++x)
                _d->velocity_y_previous[fluid_2d_p::index(x, y)] -=
                    _d->density[fluid_2d_p::index(x, y)] * _d->buoyancy * dt;

    add_source(_d->density, _d->density_previous, dt);
    diffuse(0, _d->density_previous, _d->density, _d->diffusion, dt, _d->solver_iterations);
    advect(0, _d->density, _d->density_previous, _d->velocity_x, _d->velocity_y, dt);
    add_source(_d->velocity_x, _d->velocity_x_previous, dt);
    add_source(_d->velocity_y, _d->velocity_y_previous, dt);
    diffuse(1, _d->velocity_x_previous, _d->velocity_x, _d->viscosity, dt, _d->solver_iterations);
    diffuse(2, _d->velocity_y_previous, _d->velocity_y, _d->viscosity, dt, _d->solver_iterations);
    std::vector<float> pressure(_d->density.size(), 0.0f);
    project(_d->velocity_x_previous, _d->velocity_y_previous, pressure,
        _d->density_previous, _d->solver_iterations);
    advect(1, _d->velocity_x, _d->velocity_x_previous, _d->velocity_x_previous, _d->velocity_y_previous, dt);
    advect(2, _d->velocity_y, _d->velocity_y_previous, _d->velocity_x_previous, _d->velocity_y_previous, dt);
    project(_d->velocity_x, _d->velocity_y, pressure,
        _d->density_previous, _d->solver_iterations);
    std::fill(_d->density_previous.begin(), _d->density_previous.end(), 0.0f);
    std::fill(_d->velocity_x_previous.begin(), _d->velocity_x_previous.end(), 0.0f);
    std::fill(_d->velocity_y_previous.begin(), _d->velocity_y_previous.end(), 0.0f);
    return true;
}

bool fluid_2d::event(SDL_Event *event)
{
    if(event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
        _d->dragging = true;
    else if(event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT)
        _d->dragging = false;
    else if(event->type == SDL_EVENT_MOUSE_MOTION && _d->dragging)
    {
        const viewport_rect viewport = get_viewport();
        const glm::vec2 position {event->motion.x, event->motion.y};
        if(position.x >= viewport.pos.x && position.x < viewport.pos.x + viewport.size.x &&
           position.y >= viewport.pos.y && position.y < viewport.pos.y + viewport.size.y)
        {
            const int center_x = std::clamp(static_cast<int>((position.x - viewport.pos.x) / viewport.size.x * GRID_WIDTH) + 1, 1, GRID_WIDTH);
            const int center_y = std::clamp(static_cast<int>((position.y - viewport.pos.y) / viewport.size.y * GRID_HEIGHT) + 1, 1, GRID_HEIGHT);
            for(int y = center_y - INJECTION_RADIUS; y <= center_y + INJECTION_RADIUS; ++y)
                for(int x = center_x - INJECTION_RADIUS; x <= center_x + INJECTION_RADIUS; ++x)
                {
                    if(x < 1 || x > GRID_WIDTH || y < 1 || y > GRID_HEIGHT)
                        continue;
                    const float distance = std::sqrt(static_cast<float>((x - center_x) * (x - center_x) +
                                                                         (y - center_y) * (y - center_y)));
                    if(distance > INJECTION_RADIUS)
                        continue;
                    const float weight = 1.0f - distance / static_cast<float>(INJECTION_RADIUS);
                    const int index = fluid_2d_p::index(x, y);
                    _d->density_previous[index] += 160.0f * weight;
                    _d->velocity_x_previous[index] += event->motion.xrel * _d->force * weight;
                    _d->velocity_y_previous[index] +=
                        (event->motion.yrel * _d->force - _d->buoyancy) * weight;
                }
        }
    }
    else if(event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_R)
        _d->clear();
    return true;
}

extern "C" void _rtti_init_fluid_2d()
{
    entt::meta_factory<nb::fluid_2d>{}
        .type("fluid_2d"_hs)
        .custom<rtti::type_info>(rtti::type_info{"fluid_2d", rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::fluid_2d>>{rtti::ctx_systems()}
        .type("fluid_2d_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::fluid_2d>>()
        .conv<std::shared_ptr<nb::system>>();
}
