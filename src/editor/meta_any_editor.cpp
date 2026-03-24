#include <newbase/editor/meta_any_editor.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/utility/glm.hpp>
#include <imgui.h>

namespace nb {

bool draw_meta_any_editor(const char* label, entt::meta_any& ref, bool recursing)
{
    if (!ref) return false;
    auto ti = ref.type().info();
    bool changed = false;

    if (ti == entt::type_id<bool>()) {
        bool v = *ref.try_cast<bool>();
        if (ImGui::Checkbox(label, &v)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<float>()) {
        float v = *ref.try_cast<float>();
        if (ImGui::DragFloat(label, &v, 0.01f)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<int>()) {
        int v = *ref.try_cast<int>();
        if (ImGui::DragInt(label, &v)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<unsigned int>()) {
        int v = (int)*ref.try_cast<unsigned int>();
        if (ImGui::DragInt(label, &v, 1, 0)) { ref.assign((unsigned int)v); changed = true; }
    } else if (ti == entt::type_id<glm::vec2>()) {
        glm::vec2 v = *ref.try_cast<glm::vec2>();
        if (ImGui::DragFloat2(label, &v.x, 0.01f)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<glm::vec3>()) {
        glm::vec3 v = *ref.try_cast<glm::vec3>();
        if (ImGui::DragFloat3(label, &v.x, 0.01f)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<glm::vec4>()) {
        glm::vec4 v = *ref.try_cast<glm::vec4>();
        if (ImGui::DragFloat4(label, &v.x, 0.01f)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<glm::quat>()) {
        glm::quat v = *ref.try_cast<glm::quat>();
        if (ImGui::DragFloat4(label, &v.x, 0.001f)) { ref.assign(v); changed = true; }
    } else if (ti == entt::type_id<entt::entity>()) {
        if (auto* v = ref.try_cast<entt::entity>()) ImGui::Text("%s: %x", label, entt::to_integral(*v));
    } else if (ti == entt::type_id<std::string>()) {
        if (auto* v = ref.try_cast<std::string>()) {
            char buf[256]; strncpy(buf, v->c_str(), 255); buf[255] = 0;
            if (ImGui::InputText(label, buf, sizeof(buf))) { *v = buf; changed = true; }
        }
    } else {
        auto type = ref.type();
        auto data_range = type.data();
        bool has_data = data_range.begin() != data_range.end();
        if (has_data && (!recursing || ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))) {
            for (auto [did, d] : data_range) {
                const rtti::data_info* di = d.custom().operator const rtti::data_info*();
                const char* fname = di ? di->identifier.operator const char*() : "?";
                auto member = d.get(ref);
                if (draw_meta_any_editor(fname, member, true))
                {
                    d.set(ref, member);
                    changed = true;
                }
            }
            if(recursing)
                ImGui::TreePop();
        } else if (!has_data) {
            ImGui::LabelText(label, "(unknown type)");
        }
    }
    return changed;
}

} // namespace nb
