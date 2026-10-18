#include <newbase/ui/meta_any_editor.hpp>
#include <newbase/ui/resource_field_widget.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/utility/glm.hpp>
#include <newbase/res/resource.hpp>
#include <newbase/log.hpp>
#include <imgui.h>
#include <memory>
#include <string>

namespace nb {

bool draw_meta_any_editor(const char* label, entt::meta_any& ref, bool recursing, const rtti::data_info* hint)
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
        if (hint && hint->subtype == rtti::DATA_SUBTYPE_COLOR) {
            if (ImGui::ColorEdit3(label, &v.x)) { ref.assign(v); changed = true; }
        } else {
            if (ImGui::DragFloat3(label, &v.x, 0.01f)) { ref.assign(v); changed = true; }
        }
    } else if (ti == entt::type_id<glm::vec4>()) {
        glm::vec4 v = *ref.try_cast<glm::vec4>();
        if (hint && hint->subtype == rtti::DATA_SUBTYPE_COLOR) {
            if (ImGui::ColorEdit4(label, &v.x)) { ref.assign(v); changed = true; }
        } else {
            if (ImGui::DragFloat4(label, &v.x, 0.01f)) { ref.assign(v); changed = true; }
        }
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
    } else if (ti == entt::type_id<entt::meta_any>())
    {
        auto *any = ref.try_cast<entt::meta_any>();
        if(any)
        {
            return draw_meta_any_editor(label, *any, true, hint);
        }
        else ImGui::LabelText(label, "(meta::any)");
    } else if (ref.type().is_sequence_container())
    {
        if(auto view = ref.as_sequence_container(); view) {
            int idx {0};
            for (auto iref : view)
            {
                std::string lbl {label};
                lbl += std::to_string(idx++);
                draw_meta_any_editor(lbl.c_str(), iref, true, hint);
            }
        }
    } else {
        // shared_ptr<resource> types
        auto type = ref.type();
        const rtti::type_info* type_rtti = type.custom().operator const rtti::type_info*();
        if (type_rtti && type_rtti->type_class == rtti::TYPE_CLASS_RESOURCE_PTR)
        {
            auto ptr = type_rtti->data.resource_ptr.get_ptr(ref);
            if (draw_resource_field(label, type_rtti->data.resource_ptr.resource_type_id, ptr))
            {
                type_rtti->data.resource_ptr.set_ptr(ref, ptr);
                return true;
            }
            return false;
        }

        auto data_range = type.data();
        bool has_data = data_range.begin() != data_range.end();
        if (has_data && (!recursing
            
             || ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))) {
            for (auto [did, d] : data_range) {
                const rtti::data_info* di = d.custom().operator const rtti::data_info*();
                const char* fname = di ? di->identifier.operator const char*() : "?";
                auto member = d.get(ref);
                if (draw_meta_any_editor(fname, member, true, di))
                {
                    d.set(ref, member);
                    changed = true;
                }
            }
            if(recursing)
                ImGui::TreePop();
        } else if (!has_data) {
            ImGui::LabelText(label, "(? '%s' 0x%08x)",
                             std::string(ref.type().info().name()).c_str(), ref.type().id());
        }
    }
    return changed;
}

} // namespace nb
