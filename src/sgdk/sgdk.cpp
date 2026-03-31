#include <newbase/engine.hpp>
#include <newbase/sgdk/sgdk.hpp>
#include <newbase/sgdk/sgdk_p.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/services/renderer_service.hpp>
#include <entt/locator/locator.hpp>
#include <imgui.h>
#include <algorithm>

// global state defined here
thread_local ::nb::sgdk_p* ::nb::tl_current = nullptr;
void (*::nb::g_pending_game_main)(bool) = nullptr;

using namespace nb;


// ---------------------------------------------------------------------------
// Game thread entry
// ---------------------------------------------------------------------------
static int game_thread_fn(void* data)
{
    sgdk_p* p = static_cast<sgdk_p*>(data);
    nb::tl_current = p;

    SDL_WaitSemaphore(p->sem_game);

    if (!p->exit_requested) {
        if (!p->game_main) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "[sgdk] no game main registered — call nb_sgdk_set_main() before engine init");
        } else if (setjmp(p->exit_jmp) == 0) {
            p->game_main(true);
        }
    }

    p->exited = true;
    SDL_SignalSemaphore(p->sem_host);
    return 0;
}

// ---------------------------------------------------------------------------
// sgdk_p helpers
// ---------------------------------------------------------------------------
bool sgdk_p::init()
{
    sem_host = SDL_CreateSemaphore(0);
    sem_game = SDL_CreateSemaphore(0);
    if (!sem_host || !sem_game)
        return false;

    if (!game_main)
    {
        game_main = ::nb::g_pending_game_main;
        ::nb::g_pending_game_main = nullptr;
    }

    thread = SDL_CreateThread(game_thread_fn, "sgdk_game", this);
    return thread != nullptr;
}

void sgdk_p::shutdown()
{
    if (thread) {
        exit_requested = true;
        SDL_SignalSemaphore(sem_game);
        SDL_WaitThread(thread, nullptr);
        thread = nullptr;
    }
    if (sem_host) { SDL_DestroySemaphore(sem_host); sem_host = nullptr; }
    if (sem_game) { SDL_DestroySemaphore(sem_game); sem_game = nullptr; }

    auto* rs = entt::locator<renderer_service*>::has_value()
               ? entt::locator<renderer_service*>::value() : nullptr;
    if (rs && vdp_tex)   { rs->destroy_texture(vdp_tex); vdp_tex = nullptr; }
    if (vdp_surface)     { SDL_DestroySurface(vdp_surface); vdp_surface = nullptr; }
}

// ---------------------------------------------------------------------------
// nb::sgdk system
// ---------------------------------------------------------------------------
sgdk::sgdk() : _d(new sgdk_p) {}

sgdk::~sgdk()
{
    auto* ui_mgr = entt::locator<ui_manager*>::has_value()
                   ? entt::locator<ui_manager*>::value() : nullptr;
    if (ui_mgr) ui_mgr->unregister_tool_window("sgdk vdp");
    _d->shutdown();
    delete _d;
}

bool sgdk::init(ryml::ConstNodeRef /*cfg*/)
{
    constexpr int VDP_W = 320, VDP_H = 224;

    // --- VDP surface (CPU-side pixel buffer) ---
    _d->vdp_surface = SDL_CreateSurface(VDP_W, VDP_H, SDL_PIXELFORMAT_RGBA32);
    if (!_d->vdp_surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[sgdk] failed to create VDP surface");
        return false;
    }
    SDL_ClearSurface(_d->vdp_surface, 0.f, 0.f, 0.f, 1.f);

    // --- GPU texture ---
    auto* rs = entt::locator<renderer_service*>::has_value()
               ? entt::locator<renderer_service*>::value() : nullptr;
    if (rs) {
        _d->vdp_tex = rs->create_texture(VDP_W, VDP_H);
        rs->update_texture(_d->vdp_tex, _d->vdp_surface->pixels, _d->vdp_surface->pitch);
    }

    // --- ImGui tool window ---
    auto* ui_mgr = entt::locator<ui_manager*>::has_value()
                   ? entt::locator<ui_manager*>::value() : nullptr;
    if (ui_mgr) {
        ui_mgr->register_tool_window("sgdk vdp", [this](bool* open) {
            if (!ImGui::Begin("SGDK VDP", open)) { ImGui::End(); return; }

            if (_d->vdp_tex) {
                constexpr float W = 320.f, H = 224.f;
                ImVec2 avail = ImGui::GetContentRegionAvail();
                float scale  = std::min(avail.x / W, avail.y / H);
                ImGui::Image((ImTextureID)_d->vdp_tex, ImVec2(W * scale, H * scale));
            } else {
                ImGui::TextDisabled("(no renderer service)");
            }

            ImGui::End();
        });
    }

    engine::instance().debug_action_register("VDP toggle", [](){
        ui_manager* ui_mgr = entt::locator<ui_manager*>::value();
        if(ui_mgr)
            ui_mgr->toggle_tool_window("vdp");
    }, 7);

    // --- Game thread ---
    if (!_d->init()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[sgdk] failed to create game thread");
        return false;
    }

    SDL_Log("[sgdk] ready — VDP %dx%d", VDP_W, VDP_H);
    return true;
}

bool sgdk::step(step_phase phase)
{
    if (phase == GENERAL_UPDATE) {
        if (_d->exited)
            return true;

        SDL_SignalSemaphore(_d->sem_game);
        SDL_WaitSemaphore(_d->sem_host);
    }

    if (phase == RENDER) {
        _d->vdp.render_frame(_d->vdp_surface);

        auto* rs = entt::locator<renderer_service*>::has_value()
                   ? entt::locator<renderer_service*>::value() : nullptr;
        if (rs && _d->vdp_tex)
            rs->update_texture(_d->vdp_tex, _d->vdp_surface->pixels, _d->vdp_surface->pitch);
    }

    return true;
}

bool sgdk::event(SDL_Event* /*ev*/)
{
    return true;
}

// ---------------------------------------------------------------------------
// RTTI
// ---------------------------------------------------------------------------
#include <newbase/reflection/data.hpp>
#include <newbase/reflection/contexts.hpp>
#include <entt/meta/factory.hpp>

extern "C" void _rtti_init_sgdk()
{
    using namespace entt::literals;

    entt::meta_factory<nb::sgdk>{}
        .type("sgdk"_hs)
        .custom<nb::rtti::type_info>(nb::rtti::type_info{"sgdk", nb::rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>();

    entt::meta_factory<std::shared_ptr<nb::sgdk>>{nb::rtti::ctx_systems()}
        .type("sgdk_shared"_hs)
        .ctor<&nb::rtti::shared_ptr_builder<nb::sgdk>>()
        .conv<std::shared_ptr<nb::system>>();
}

