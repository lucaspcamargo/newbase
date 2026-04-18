#include <newbase/ui/manager.hpp>
#include <newbase/ui/imgui_style.hpp>
#include <newbase/ui/imgui_icons.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/log.hpp>
#ifdef __EMSCRIPTEN__
#include <newbase/utility/emscripten.hpp>
#endif
// for draw_perf
#include <newbase/nb_config.h>
#include <newbase/engine.hpp>
#include <newbase/system.hpp>

#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "tracy/Tracy.hpp"
#include <unordered_map>
#include <string>

using namespace nb;

struct tool_window_data
{
    std::string name {};
    std::function<void(bool*)> draw_fn {};
    bool enabled = false;
};

struct nb::ui_manager_p
{
    bool editor_mode = false;
    std::unordered_map<std::string, tool_window_data> tool_windows;
    ImGuiID dockspace_id;
    std::string ini_path;
    ui_manager::open_resource_editor_fn open_resource_editor_cb;
};


ui_manager_simple::ui_manager_simple()
{
    _d = new ui_manager_p();
    log::info("[ui_manager] initialized");
}

ui_manager_simple::~ui_manager_simple()
{
    log::info("[ui_manager] syncing ui settings");
    ImGui::SaveIniSettingsToDisk(_d->ini_path.c_str());
#ifdef __EMSCRIPTEN__
    ::nb::ems::sync_pref_path();
#endif
    delete _d;
    log::info("[ui_manager] destroyed");
}

bool ui_manager_simple::ui_init()
{
    // Setup Dear ImGui context
    char * prefs = SDL_GetPrefPath(
        SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING), 
        SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING));
    std::string imgui_cfg_path {prefs};
    SDL_free(prefs);
    
    assert(imgui_cfg_path.size());
    if(imgui_cfg_path[imgui_cfg_path.size()-1]!='/')
    imgui_cfg_path += "/";
    imgui_cfg_path += "ImGui.cfg";
    log::info("[ui_manager] cfg file at: '%s'", imgui_cfg_path.c_str());
    _d->ini_path = imgui_cfg_path;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // we drive saving manually to hook post-save sync
    io.IniSavingRate = 1.0f;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    ImGui::LoadIniSettingsFromDisk(_d->ini_path.c_str());

    imgui_style_setup();

    return true;
}

void ui_manager_simple::ui_init_finish(float scale)
{
    // for android, scaling works a bit differently
#ifdef ANDROID
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::GetIO().FontGlobalScale = scale;
#endif

    imgui_style_fonts_setup(scale);
}


void ui_manager_simple::ui_new_frame(int safe_x, int safe_y, int safe_w, int safe_h)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantSaveIniSettings) {
        ImGui::SaveIniSettingsToDisk(_d->ini_path.c_str());
        io.WantSaveIniSettings = false;
#ifdef __EMSCRIPTEN__
        ::nb::ems::sync_pref_path();
#endif
    }

    ImGui::NewFrame();
    _d->dockspace_id = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::GetMainViewport()->WorkPos.x = static_cast<float>(safe_x);
    ImGui::GetMainViewport()->WorkPos.y = static_cast<float>(safe_y);
    ImGui::GetMainViewport()->WorkSize.x = static_cast<float>(safe_w);
    ImGui::GetMainViewport()->WorkSize.y = static_cast<float>(safe_h);
}

void ui_manager_simple::ui_destroy()
{
    log::info("[ui_manager] ui context destroy");
    ImGui::DestroyContext();
}

void ui_manager_simple::register_tool_window(const char* name, std::function<void(bool*)> draw_fn)
{
    _d->tool_windows[name] = {name, draw_fn, false};
    log::info("[ui_manager] registered tool window '%s'", name);
}

void ui_manager_simple::unregister_tool_window(const char* name)
{
    _d->tool_windows.erase(name);
    log::info("[ui_manager] unregistered tool window '%s'", name);
}

void ui_manager_simple::draw_tool_windows()
{
    for(auto& pair: _d->tool_windows)
    {
        if(!pair.second.enabled)
            continue;
        const std::string& name = pair.first;
        auto& draw_fn = pair.second.draw_fn;
        draw_fn(&(pair.second.enabled));
    }
}

bool ui_manager_simple::toggle_tool_window(const char *name)
{
    auto it = _d->tool_windows.find(name);
    if(it != _d->tool_windows.end())
    {
        if(it->second.enabled)
        {
            log::info("[ui_manager] toggling tool window '%s' off", name);
            it->second.enabled = false;
        }
        else
        {
            log::info("[ui_manager] toggling tool window '%s' on", name);
            it->second.enabled = true;
        }
        return true;
    }
    return false;
}

void ui_manager_simple::register_open_resource_editor_callback(open_resource_editor_fn fn)
{
    _d->open_resource_editor_cb = std::move(fn);
}

void ui_manager_simple::request_open_resource_editor(entt::id_type type_id, entt::id_type asset_id, std::string_view name)
{
    if (_d->open_resource_editor_cb)
        _d->open_resource_editor_cb(type_id, asset_id, name);
}

static float get_frametime_point(void *data, int index)
{
    if(!data)
        return 0.0f;

    index = index % NB_FRAMECOUNTER_SAMPLES;

    const engine::framecounter_data* fc_data = static_cast<const engine::framecounter_data*>(data);
    uint64_t delta_ns = fc_data->fc_phase_end[index] - fc_data->fc_phase_start[index];
    return delta_ns * 1000.0f;
}

static ImU32 frametimes_bar_color(float t)
{
    // t=0: 120fps (blue), t=0.33: 60fps (green), t=0.66: 30fps (yellow), t=1: 15fps (red)
    float r, g, b;
    if (t < 1.0f/3.0f) {
        float f = t * 3.0f;
        r = 0.0f; g = f; b = 1.0f - f;
    } else if (t < 2.0f/3.0f) {
        float f = (t - 1.0f/3.0f) * 3.0f;
        r = f; g = 1.0f; b = 0.0f;
    } else {
        float f = (t - 2.0f/3.0f) * 3.0f;
        r = 1.0f; g = 1.0f - f; b = 0.0f;
    }
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
}

static void draw_frametimes_bar_graph(const engine::framecounter_data& fc, int fc_end)
{
    // for more on this: https://web.archive.org/web/20250323005822/https://asawicki.info/news_1758_an_idea_for_visualization_of_frame_times

    const auto &col_bg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const auto &col_outline = ImGui::GetStyleColorVec4(ImGuiCol_Border);

    const float canvas_h    = 50.0f;
    const float min_h       = 2.0f;
    const float max_h       = canvas_h - 4.0f;
    const float min_dt      = 1.0f / 120.0f;  // 120 fps
    const float max_dt      = 1.0f / 15.0f;   // 15 fps
    const float log2_min_dt = log2f(min_dt);
    const float log2_range  = log2f(max_dt) - log2_min_dt;

    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    float canvas_w = ImGui::GetContentRegionAvail().x;
    if (canvas_w < 150.0f) 
        canvas_w = 150.0f;
    ImGui::Dummy(ImVec2(canvas_w, canvas_h));
    ImVec2 post_canvas_pos = ImGui::GetCursorScreenPos();
    ImGui::SetNextItemAllowOverlap();
    ImGui::SetCursorScreenPos(canvas_pos);
    ImGui::TextAligned(0.5f, ImGui::GetContentRegionAvail().x, "Sawicki");
    ImGui::SetCursorScreenPos(post_canvas_pos);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvas_pos,
        ImVec2(canvas_pos.x + canvas_w, canvas_pos.y + canvas_h),
        ImGui::ColorConvertFloat4ToU32(col_bg));
    dl->AddRect(canvas_pos,
        ImVec2(canvas_pos.x + canvas_w, canvas_pos.y + canvas_h),
        ImGui::ColorConvertFloat4ToU32(col_outline));

    float x = canvas_pos.x + canvas_w;
    for (int i = 0; i < NB_FRAMECOUNTER_SAMPLES && x > canvas_pos.x; i++)
    {
        int idx = ((fc_end - i) % NB_FRAMECOUNTER_SAMPLES + NB_FRAMECOUNTER_SAMPLES) % NB_FRAMECOUNTER_SAMPLES;
        uint64_t start = fc.fc_phase_start[idx];
        uint64_t end   = fc.fc_phase_end[idx];
        if (start == 0 || end <= start)
            continue;

        float dt = (end - start) * 1e-9f;  // nanoseconds -> seconds

        float frame_w = dt / min_dt;
        float draw_right = ceilf(x);
        float draw_left  = floorf(x - frame_w);
        if (draw_right - draw_left < 1.0f)
            draw_left = draw_right - 1.0f;

        float factor = (log2f(fmaxf(dt, min_dt)) - log2_min_dt) / log2_range;
        factor = fmaxf(0.0f, fminf(1.0f, factor));
        float bar_h = min_h + factor * (max_h - min_h);

        float top_y = canvas_pos.y + canvas_h - bar_h;
        float bot_y = canvas_pos.y + canvas_h;

        dl->AddRectFilled(ImVec2(draw_left-1, top_y-1), ImVec2(draw_right-1, bot_y-1),
            frametimes_bar_color(factor));

        x -= frame_w;
    }
}

void ui_manager_simple::draw_perf()
{
    const float PAD = ImGui::GetFontSize() * 0.75f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 work_pos = viewport->WorkPos; // Use work area to avoid menu-bar/task-bar, if any!
    ImVec2 work_size = viewport->WorkSize;

    ImGuiDockNode* central_node = ImGui::DockBuilderGetCentralNode(static_cast<ImGuiID>(_d->dockspace_id));
    if (central_node)
    {
        // If there is a DockSpace, use its bounds instead of the entire viewport
        work_pos = central_node->Pos;
        work_size = central_node->Size;
    }

    // Keep the renderer's default viewport in sync with the central node.
    // This runs every frame regardless of whether the perf overlay is drawn.
    if(auto *rend = entt::locator<renderer_service*>::value())
    {
        viewport_handle dvp = rend->default_viewport();
        if(dvp != VIEWPORT_INVALID && work_size.x > 1.f && work_size.y > 1.f)
        {
            const ImGuiIO &io = ImGui::GetIO();
            float sx = io.DisplayFramebufferScale.x > 0.f ? io.DisplayFramebufferScale.x : 1.f;
            float sy = io.DisplayFramebufferScale.y > 0.f ? io.DisplayFramebufferScale.y : 1.f;
            rend->update_viewport(dvp,
                static_cast<int>(work_pos.x  * sx), static_cast<int>(work_pos.y  * sy),
                static_cast<int>(work_size.x * sx), static_cast<int>(work_size.y * sy));
        }
    }

    if (central_node && central_node->Windows.Size > 0)
        return;

    // maybe draw perf window

    static int location = 1;
    ImGuiIO& io = ImGui::GetIO();
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (location >= 0)
    {
        ImVec2 window_pos, window_pos_pivot;
        window_pos.x = (location & 1) ? (work_pos.x + work_size.x - PAD) : (work_pos.x + PAD);
        window_pos.y = (location & 2) ? (work_pos.y + work_size.y - PAD) : (work_pos.y + PAD);
        window_pos_pivot.x = (location & 1) ? 1.0f : 0.0f;
        window_pos_pivot.y = (location & 2) ? 1.0f : 0.0f;
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        window_flags |= ImGuiWindowFlags_NoMove;
    }
    else if (location == -2)
    {
        // Center window
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        window_flags |= ImGuiWindowFlags_NoMove;
    }
    ImGui::SetNextWindowBgAlpha(0.5f); // Transparent background
    if (ImGui::Begin("fps overlay", nullptr, window_flags))
    {
        ImGui::Text("%s %s", SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING),
                    SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING));
        ImGui::Separator();

        if(ImGui::TreeNodeEx("Frame Times", ImGuiTreeNodeFlags_NoTreePushOnOpen))
        {
            int fc_end = engine::instance().frametime_data_offset();
            int fc_offset = fc_end + 1;
            float scale_min = 0.0f;
            float scale_max = 3e10f;                                                                                                                                        
            ImVec2 graph_size {0, 50};
            ImGui::PlotLines("##frametimes_phys", &get_frametime_point, &engine::instance().frametime_data(static_cast<int>(step_phase::PHYSICS_UPDATE)), 
                NB_FRAMECOUNTER_SAMPLES, fc_offset, "Physics", scale_min, FLT_MAX, graph_size);
            ImGui::PlotLines("##frametimes_update", &get_frametime_point, &engine::instance().frametime_data(static_cast<int>(step_phase::GENERAL_UPDATE)), 
                NB_FRAMECOUNTER_SAMPLES, fc_offset, "Update", scale_min, FLT_MAX, graph_size);
            ImGui::PlotLines("##frametimes_render", &get_frametime_point, &engine::instance().frametime_data(static_cast<int>(step_phase::RENDER)), 
                NB_FRAMECOUNTER_SAMPLES, fc_offset, "Render & Swap", scale_min, scale_max, graph_size);
            ImGui::PlotLines("##frametimes_total", &get_frametime_point, &engine::instance().frametime_data(static_cast<int>(step_phase::_STEP_PHASE_COUNT)),
                NB_FRAMECOUNTER_SAMPLES, fc_offset, "Total", scale_min, scale_max, graph_size);
    
            draw_frametimes_bar_graph(engine::instance().frametime_data(static_cast<int>(step_phase::_STEP_PHASE_COUNT)), fc_end);
        }

        ImGui::Text("FPS: %.1f", io.Framerate);
        //ImGui::Text("GUI: %d vtx, %d ind", io.MetricsRenderVertices, io.MetricsRenderIndices, io.MetricsRenderIndices / 3);
#ifdef TRACY_ENABLED
        ImGui::Separator();
        ImgGui::Text("Trace: %d", static_cast<int>(TracyIsConnected));
#endif
        ImGui::Separator();    }

    // have a button to trigger another debug action menu (hamburger icon)
    if(ImGui::Button(ICON_FK_PLAY_CIRCLE " Actions"))
        ImGui::OpenPopup("DebugActionsPopup");
    
    ImGui::SameLine();

    if(ImGui::Button(ICON_FK_ARROWS " Position"))
        ImGui::OpenPopup("MovePopup");

    if(ImGui::BeginPopup("DebugActionsPopup"))
    {
        for(const auto& [idx, name]: engine::instance().debug_action_names())
        {
            if (ImGui::MenuItem(name.c_str(), NULL, false))
            {
                engine::instance().debug_action_trigger(idx);
            }
        }
        ImGui::EndPopup();
    }

    if(ImGui::BeginPopup("MovePopup"))
    {
        if(ImGui::Selectable("Top-Left", location ==0)) location = 0;
        if(ImGui::MenuItem("Top-Right")) location = 1;
        if(ImGui::MenuItem("Bottom-Left")) location = 2;
        if(ImGui::MenuItem("Bottom-Right")) location = 3;
        if(ImGui::MenuItem("Center")) location = -2;
        if(ImGui::MenuItem("Free")) location = -1;
        ImGui::EndPopup();
    }

    ImGui::End();


    // bottom hints

    std::string bottom_text {};

    for(const auto& [idx, name]: engine::instance().debug_action_names())
    {
        char c = idx == 0? '`' : '0' + idx;
        bottom_text += idx == 0 ? "[" + std::string(1, c) + "]  " + name : "[F" + std::string(1, c) + "] " + name;
        bottom_text += "      ";
    }

    // write text to background draw list
    // align to bottom left of work area, with some padding
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    auto col_text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    auto bottom_text_pos = ImVec2(work_pos.x + PAD, work_pos.y + work_size.y - PAD - ImGui::GetTextLineHeight());
    col_text.w *= 0.5f; // make text more transparent
    dl->AddText(bottom_text_pos, ImGui::ColorConvertFloat4ToU32(col_text),
        bottom_text.c_str());
}