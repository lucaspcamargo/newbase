#include "demo_system.hpp"
#include <newbase/engine.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/audio/audio.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/ui/markdown.hpp>
#include <entt/locator/locator.hpp>
#include <entt/entt.hpp>
#include <imgui.h>
#include <memory>
#include <string>
#include <cstdlib>
#include <cstring>

using entt::operator""_hs;

struct demo_entry {
    const char*   name;
    entt::id_type scene_id;
    entt::id_type readme_id;
};

static const demo_entry s_demos[] = {
    { "Hello World", "res/hello_world/scene.et.yaml"_hs,   "res/hello_world/README.md"_hs   },
    { "Asteroids",   "res/asteroids/title.et.yaml"_hs,     "res/asteroids/README.md"_hs     },
    { "Physics 2D",  "res/physics2d_demo/scene.et.yaml"_hs,"res/physics2d_demo/README.md"_hs},
    { "Platformer",  "res/platformer/scene.et.yaml"_hs,    "res/platformer/README.md"_hs    },
    { "Fast Rodent", "res/fast_rodent/scene.et.yaml"_hs,  "res/fast_rodent/README.md"_hs   },
};
static constexpr int s_demo_count = static_cast<int>(sizeof(s_demos) / sizeof(s_demos[0]));

static int s_current = 0;

static void load_demo(int idx)
{
    s_current = idx;
    auto audio_system = nb::engine::instance().system_from_id(entt::hashed_string{"audio"}.value());
    if(audio_system)
    {
        // stop all sounds
        auto audio = static_cast<nb::audio*>(audio_system.get());
        audio->bgm_stop();  // TODO stop sfx and reset audio graph state as well
    }
    nb::engine::instance().request_scene_change(s_demos[idx].scene_id);
}

bool demo_system::init(ryml::ConstNodeRef)
{
    // Parse --demo <index> from command line
    int initial_demo = 0;
    bool demo_specified = false;
    int argc = nb::engine::instance().argc();
    char** argv = nb::engine::instance().argv();
    for (int i = 1; i < argc; ++i)
    {
        if ((strcmp(argv[i], "--demo") == 0 || strcmp(argv[i], "-demo") == 0) && i + 1 < argc)
        {
            int idx = atoi(argv[i + 1]);
            if (idx >= 0 && idx < s_demo_count)
            {
                initial_demo  = idx;
                demo_specified = true;
            }
            break;
        }
    }

    auto* ui = entt::locator<nb::ui_manager*>::value();
    if (!ui) return true;

    ui->register_tool_window("demo", [](bool* open) {
        ImGui::Begin("Demo", open);

        ImGui::SetWindowFontScale(2.0f);
        ImGui::Text("newbase demo");
        ImGui::SetWindowFontScale(1.0f);

        ImGui::Separator();
        ImGui::Text("Current demo:");

        static const char* names[s_demo_count];
        for (int i = 0; i < s_demo_count; ++i)
            names[i] = s_demos[i].name;

        ImGui::SetWindowFontScale(1.5f);
        ImGui::PushItemWidth(-1);
        int sel = s_current;
        if (ImGui::Combo("##combo", &sel, names, s_demo_count) && sel != s_current)
            load_demo(sel);
        ImGui::PopItemWidth();
        ImGui::SetWindowFontScale(1.0f);

        ImGui::Separator();

        // Load README on first access for each demo (cached as std::string)
        static std::string s_readmes[s_demo_count];
        if (s_readmes[s_current].empty())
        {
            std::vector<char> buf;
            if (nb::rman().read_all_sync(s_demos[s_current].readme_id, buf, true))
                s_readmes[s_current] = buf.data();
        }
        if (!s_readmes[s_current].empty())
            nb::ui::Markdown(s_readmes[s_current]);
        else
            ImGui::TextDisabled("(no description)");

        ImGui::End();
    });
    if (!demo_specified)
        ui->toggle_tool_window("demo");

    nb::engine::instance().debug_action_register("Demo Controls", []() {
        if (auto* u = entt::locator<nb::ui_manager*>::value())
            u->toggle_tool_window("demo");
    });

    load_demo(initial_demo);

    return true;
}

extern "C" void _nb_demo_register_systems()
{
    nb::engine::instance().register_system(std::make_shared<demo_system>());
}
