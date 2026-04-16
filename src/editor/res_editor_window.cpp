#include <newbase/editor/res_editor_window.hpp>
#include <newbase/editor/meta_any_editor.hpp>
#include <newbase/editor/texture_editor_widget.hpp>
#include <newbase/editor/audio_editor_widget.hpp>
#include <newbase/editor/text_editor_widget.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/wav.hpp>
#include <newbase/res/vorbis.hpp>
#include <newbase/res/script.hpp>
#include <newbase/res/yaml.hpp>
#include <imgui.h>
#include "IconsForkAwesome.h"

using entt::operator""_hs;

namespace nb {

res_editor_window::res_editor_window() = default;
res_editor_window::~res_editor_window() = default;
res_editor_window::res_editor_window(res_editor_window&&) noexcept = default;
res_editor_window& res_editor_window::operator=(res_editor_window&&) noexcept = default;

void res_editor_window::open(entt::id_type type_id, entt::id_type asset_id, std::string_view title)
{
    _type_id  = type_id;
    _asset_id = asset_id;
    _resource = rman().get(type_id, asset_id);
    _ref      = {};
    _tex_widget.reset();
    _audio_widget.reset();
    _text_widget.reset();

    // build a unique imgui window id: visible title + hidden id suffix
    _title = std::string(title) + "##resed_" + std::to_string(asset_id);

    if (_resource)
    {
        // Texture: open the dedicated paint widget
        if (_resource->type_id() == "rtexture"_hs.value())
        {
            auto* rt = static_cast<rtexture*>(_resource.get());
            // Surface may have been freed after GPU upload — reload it on demand.
            if (!rt->surf && rt->reload_surface)
                rt->surf = rt->reload_surface(rt->id());
            if (rt->surf)
            {
                _tex_widget = std::make_unique<texture_editor_widget>();
                _tex_widget->open(rt->surf);
            }
        }
        // WAV
        if (_resource->type_id() == "rwav"_hs.value())
        {
            auto* rw = static_cast<rwav*>(_resource.get());
            if (rw->valid && rw->buf && rw->len > 0)
            {
                _audio_widget = std::make_unique<audio_editor_widget>();
                _audio_widget->open(rw->buf, rw->len, rw->spec);
            }
        }

        // Vorbis
        if (_resource->type_id() == "rvorbis"_hs.value())
        {
            auto* rv = static_cast<rvorbis*>(_resource.get());
            if (rv->cached && !rv->frames.empty())
            {
                _audio_widget = std::make_unique<audio_editor_widget>();
                _audio_widget->open(rv->frames.data(), rv->frames.size(), rv->spec);
            }
        }

        // Lua script
        if (_resource->type_id() == "rscript"_hs.value())
        {
            auto* rs = static_cast<rscript*>(_resource.get());
            if (rs->valid && !rs->raw.empty())
            {
                _text_widget = std::make_unique<text_editor_widget>();
                _text_widget->open(rs->raw.data(), rs->raw.size(), "Lua");
            }
        }

        // YAML
        if (_resource->type_id() == "ryaml"_hs.value())
        {
            auto* ry = static_cast<ryaml*>(_resource.get());
            if (ry->yaml_valid && !ry->data.empty())
            {
                _text_widget = std::make_unique<text_editor_widget>();
                // data is null-terminated; exclude the terminator from the length
                size_t text_len = ry->data.size();
                if (text_len > 0 && ry->data.back() == '\0') --text_len;
                _text_widget->open(ry->data.data(), text_len, "YAML");
            }
        }

        // Always build a meta_any too (shown as fallback or alongside)
        auto meta_type = entt::resolve(type_id);
        if (meta_type)
            _ref = meta_type.from_void(dynamic_cast<void*>(_resource.get()));
    }
}

void res_editor_window::draw(bool* p_open)
{
    if (!ImGui::Begin(_title.c_str(), p_open, ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::End();
        return;
    }

    auto meta_type = entt::resolve(_type_id);
    const rtti::type_info* info = meta_type ? meta_type.custom().operator const rtti::type_info*() : nullptr;
    const char* type_name = info ? info->identifier.operator const char*() : "?";
    ImGui::TextDisabled("%s  [id %x]", type_name, _asset_id);
    ImGui::Separator();

    if (_tex_widget)
    {
        if (ImGui::Button(ICON_FK_CHECK " Apply"))
            _tex_widget->apply(_resource.get());
        ImGui::SameLine();
        if (ImGui::Button(ICON_FK_FLOPPY_O " Save"))
        {
            _tex_widget->apply(_resource.get());
            rman().save_resource(_resource.get());
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FK_REFRESH " Reload"))
        {
            // load_nocache: fresh copy from disk, cache untouched
            auto fresh = rman().load_nocache(_type_id, _asset_id);
            if (fresh)
            {
                auto* rt = static_cast<rtexture*>(fresh.get());
                if (!rt->surf && rt->reload_surface)
                    rt->surf = rt->reload_surface(rt->id());
                if (rt->surf)
                {
                    _tex_widget = std::make_unique<texture_editor_widget>();
                    _tex_widget->open(rt->surf);
                }
            }
            // fresh drops here — cache and existing holders are unaffected
        }
        ImGui::Separator();
        _tex_widget->draw();
    }
    else if (_audio_widget)
    {
        _audio_widget->draw();
    }
    else if (_text_widget)
    {
        _text_widget->draw();
    }
    else if (_ref)
        draw_meta_any_editor(type_name, _ref);
    else
        ImGui::TextDisabled("(resource not loaded)");

    ImGui::End();
}

} // namespace nb
