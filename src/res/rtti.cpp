#include <newbase/res/rtti.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/res/loaders.hpp>
#include <newbase/res/writers.hpp>
#include <newbase/res/etree.hpp>
#include <newbase/res/sprite.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/script.hpp>
#include <newbase/res/vorbis.hpp>
#include <newbase/res/wav.hpp>
#include <newbase/res/yaml.hpp>
#include <entt/meta/factory.hpp>
#include "IconsForkAwesome.h"

using entt::operator""_hs;

namespace nb::rtti {

    void _rtti_init_resources()
    {
        entt::meta_factory<rscript>{}
            .type("rscript"_hs)
            .custom<type_info>(type_info{
                .identifier = "script",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_CODE_O, .extensions = "lua"}},
                .loader_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                    return rloader_script{}(id);
                }
            });

        entt::meta_factory<rtexture>{}
            .type("rtexture"_hs)
            .custom<type_info>(type_info{
                .identifier = "texture",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_IMAGE_O, .extensions = "png jpg jpeg bmp"}},
                .loader_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                    return rloader_texture{}(id);
                },
                .saver_fn = rwriter_texture,
            });

        entt::meta_factory<rsprite>{}
            .type("rsprite"_hs)
            .custom<type_info>(type_info{
                .identifier = "sprite",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_IMAGE_O, .extensions = "sprite"}},
                .loader_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                    return rloader_sprite{}(id);
                }
            });

        entt::meta_factory<rvorbis>{}
            .type("rvorbis"_hs)
            .custom<type_info>(type_info{
                .identifier = "vorbis",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_AUDIO_O, .extensions = "ogg"}},
                .loader_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                    return rloader_vorbis{}(id);
                }
            });

        entt::meta_factory<rwav>{}
            .type("rwav"_hs)
            .custom<type_info>(type_info{
                .identifier = "wav",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_AUDIO_O, .extensions = "wav"}},
                .loader_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                    return rloader_wav{}(id);
                }
            });

        entt::meta_factory<ryaml>{}
            .type("ryaml"_hs)
            .custom<type_info>(type_info{
                .identifier = "yaml",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_TEXT_O, .extensions = "yaml yml"}},
                .loader_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                    return rloader_yaml{}(id);
                }
            });

        entt::meta_factory<retree>{}
            .type("retree"_hs)
            .custom<type_info>(type_info{
                .identifier = "etree",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_TEXT_O, .extensions = "etree"}},
                .loader_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                    return rloader_etree{}(id);
                }
            });
    }

}
