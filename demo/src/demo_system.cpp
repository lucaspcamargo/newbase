#include "demo_system.hpp"
#include <newbase/engine.hpp>
#include <newbase/services/ui_manager.hpp>
#include <entt/locator/locator.hpp>
#include <imgui.h>
#include <memory>

bool demo_system::init(ryml::ConstNodeRef)
{
    auto* ui = entt::locator<nb::ui_manager*>::value();
    if (!ui) return true;

    ui->register_tool_window("demo", [](bool* open) {
        ImGui::Begin("Demo", open);

        // draw "newbase demo" text in double size
        ImGui::SetWindowFontScale(2.0f);
        ImGui::Text("newbase demo");
        ImGui::SetWindowFontScale(1.0f);

        ImGui::Separator();

        ImGui::Text("Current demo:");
        static const char* items[] = { "asteroids" };
        static int current = 0;
        ImGui::SetWindowFontScale(2.0f);
        // make a combo box with no label, that fills the whole width of the window, and has the current item selected
        ImGui::PushItemWidth(-1);
        ImGui::Combo("##combo", &current, items, 1);
        ImGui::PopItemWidth();
        ImGui::SetWindowFontScale(1.0f);

        ImGui::Separator();

        // draw some info about the current demo
        // for now, just mock up some wrapped long-form text about the demo
        ImGui::TextWrapped("This asteroids game is the first demo of the newbase engine. It is meant to showcase the engine's features and capabilities, as well as provide a starting point for users to build their own games and applications. The demo is built using the engine's systems and services, without going ocverboard on complexity.");

        ImGui::End();
    });
    ui->toggle_tool_window("demo");

    nb::engine::instance().debug_action_register("Demo Controls", []() {
        if (auto* u = entt::locator<nb::ui_manager*>::value())
            u->toggle_tool_window("demo");
    });

    return true;
}

extern "C" void _nb_demo_register_systems()
{
    nb::engine::instance().register_system(std::make_shared<demo_system>());
}
