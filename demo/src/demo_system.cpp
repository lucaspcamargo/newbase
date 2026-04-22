#include "demo_system.hpp"
#include <newbase/engine.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/audio/audio.hpp>
#include <entt/locator/locator.hpp>
#include <entt/entt.hpp>
#include <imgui.h>
#include <memory>
#include <cstdlib>
#include <cstring>

using entt::operator""_hs;

struct demo_entry {
    const char*   name;
    entt::id_type scene_id;
    const char*   description;
};

static const demo_entry s_demos[] = {
    {
        "Hello World",
        "res/hello_world/scene.et.yaml"_hs,
        "A minimal scene that sets up a camera, shows a sprite, and runs a Lua script. "
        "Good starting point for understanding how entities, components, and systems fit together."
        "\n\n"
        "At any point during this demo, you may press [F1] to bring up the editor tools. This will "
        "allow you to inspect the scene, view and edit entity/component data, inspect resources, and more. Try it out!"
        "\n\n"
        "You can also bring out the console with the tilde (`) key. This will show log messages for now."
    },
    {
        "Asteroids",
        "res/asteroids/title.et.yaml"_hs,
        "A classic Asteroids clone built on top of newbase. Showcases physics, particle "
        "systems, sprites, audio, Lua scripting, and scene management all working together."
        "\n\n"
        "Use arrow keys (or joystick analog) to control the ship, and press the [a] key (or X joystick button) to fire."
    },
    {
        "Physics 2D",
        "res/physics2d_demo/scene.et.yaml"_hs,
        "A rigid-body simulation using the Box2D v3 backend. A mix of circles and boxes "
        "tumble inside a static container under gentle gravity. Enable physics debug draw "
        "from the Physics2D tool window to visualize collision shapes."
    },
    {
        "Platformer",
        "res/platformer/scene.et.yaml"_hs,
        "A classic platformer demo. Showcases tilemap loading and rendering via the Tiled "
        "JSON format, tileset support with spacing/margin, and Box2D collision from tile layers."
    },
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
        ImGui::TextWrapped("%s", s_demos[s_current].description);

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
