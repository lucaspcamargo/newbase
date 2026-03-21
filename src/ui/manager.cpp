#include <newbase/ui/manager.hpp>
#include <newbase/ui/imgui_style.hpp>
#include <newbase/log.hpp>
#include <unordered_map>
#include <string>

// for draw_perf
#include <newbase/nb_config.h>
#include <newbase/engine.hpp>
#include <newbase/system.hpp>
#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "tracy/Tracy.hpp"

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
};


ui_manager_simple::ui_manager_simple()
{
    _d = new ui_manager_p();
    log::info("[ui_manager] initialized");
}

ui_manager_simple::~ui_manager_simple()
{
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
    io.IniFilename = _d->ini_path.c_str();
    io.IniSavingRate = 1.0f;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

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

static float get_frametime_point(void *data, int index)
{
    if(!data)
        return 0.0f;

    index = index % NB_FRAMECOUNTER_SAMPLES;

    const engine::framecounter_data* fc_data = static_cast<const engine::framecounter_data*>(data);
    uint64_t delta_ns = fc_data->fc_phase_end[index] - fc_data->fc_phase_start[index];
    return delta_ns * 1000.0f;
}

void ui_manager_simple::draw_perf()
{
    static int location = 1;
    ImGuiIO& io = ImGui::GetIO();
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (location >= 0)
    {
        const float PAD = 10.0f;
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 work_pos = viewport->WorkPos; // Use work area to avoid menu-bar/task-bar, if any!
        ImVec2 work_size = viewport->WorkSize;

        ImGuiDockNode* central_node = ImGui::DockBuilderGetCentralNode(static_cast<ImGuiID>(_d->dockspace_id)); 
        if (central_node)
        {
            // if there is a window docked in the central node, abort drawing this overlay
            if(central_node->Windows.Size > 0)
                return;

            // If there is a DockSpace, use its bounds instead of the entire viewport
            work_pos = central_node->Pos;
            work_size = central_node->Size;
        }

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

        int fc_end = engine::instance().frametime_data_offset();
        int fc_offset = fc_end + 1;
        ImGui::PlotLines("##frametimes_phys", &get_frametime_point, &engine::instance().frametime_data(static_cast<int>(step_phase::PHYSICS_UPDATE)), 
            NB_FRAMECOUNTER_SAMPLES, fc_offset, "Phys");
        ImGui::PlotLines("##frametimes_update", &get_frametime_point, &engine::instance().frametime_data(static_cast<int>(step_phase::GENERAL_UPDATE)), 
            NB_FRAMECOUNTER_SAMPLES, fc_offset, "Update");
        ImGui::PlotLines("##frametimes_render", &get_frametime_point, &engine::instance().frametime_data(static_cast<int>(step_phase::RENDER)), 
            NB_FRAMECOUNTER_SAMPLES, fc_offset, "Render");
        ImGui::PlotLines("##frametimes_total", &get_frametime_point, &engine::instance().frametime_data(static_cast<int>(step_phase::_STEP_PHASE_COUNT)), 
            NB_FRAMECOUNTER_SAMPLES, fc_offset, "Total");
        
        ImGui::Text("FPS: %.1f", io.Framerate);
        ImGui::Text("GUI: %d vtx, %d ind", io.MetricsRenderVertices, io.MetricsRenderIndices, io.MetricsRenderIndices / 3);
#ifdef TRACY_ENABLED
        ImGui::Separator();
        ImgGui::Text("Trace: %d", static_cast<int>(TracyIsConnected));
#endif
        ImGui::Separator();
        for(const auto& [idx, name]: engine::instance().debug_action_names())
        {
            char c = idx == 0? '`' : '0' + idx;
            ImGui::Text("[%c] %s", c, name.c_str());
        }

        if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonLeft))
        {
            if (ImGui::MenuItem("Custom",       NULL, location == -1)) location = -1;
            if (ImGui::MenuItem("Center",       NULL, location == -2)) location = -2;
            if (ImGui::MenuItem("Top-left",     NULL, location == 0)) location = 0;
            if (ImGui::MenuItem("Top-right",    NULL, location == 1)) location = 1;
            if (ImGui::MenuItem("Bottom-left",  NULL, location == 2)) location = 2;
            if (ImGui::MenuItem("Bottom-right", NULL, location == 3)) location = 3;
            ImGui::Separator();
            // have a submenu with every debug action registered, to be able to trigger them from the overlay
            if (ImGui::BeginMenu("Debug Actions"))
            {
                for(const auto& [idx, name]: engine::instance().debug_action_names())
                {
                    if (ImGui::MenuItem(name.c_str(), NULL, false))
                    {
                        engine::instance().debug_action_trigger(idx);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}