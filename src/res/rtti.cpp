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
#include <newbase/res/particle_emitter.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include "IconsForkAwesome.h"
#include <memory>

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
            })
            .ctor<>()
            .data<&rsprite::anchor>("anchor"_hs)
                .custom<rtti::data_info>(rtti::data_info{"anchor"})
            .data<&rsprite::dims>("dims"_hs)
                .custom<rtti::data_info>(rtti::data_info{"dims"})
            .data<&rsprite::tex>("tex"_hs)
                .custom<rtti::data_info>(rtti::data_info{"tex"});

        entt::meta_factory<rparticle_emitter>{}
            .type("rparticle_emitter"_hs)
            .custom<type_info>(type_info{
                .identifier = "particle_emitter",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_STAR_O, .extensions = "particle"}},
                .loader_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                    return rloader_particle_emitter{}(id);
                }
            })
            .ctor<>()
            .data<&rparticle_emitter::max_particles>("max_particles"_hs)
                .custom<data_info>(data_info{"max_particles"})
            .data<&rparticle_emitter::tex>("tex"_hs)
                .custom<data_info>(data_info{
                    .identifier       = "tex",
                    .subtype          = DATA_SUBTYPE_RESOURCE,
                    .resource_type_id = "rtexture"_hs.value()
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

        // shared_ptr<T> registrations — used by meta_any_editor to display resource fields
#define NB_REG_RES_PTR(T, name_str) \
    entt::meta_factory<std::shared_ptr<T>>{} \
        .type(entt::hashed_string{name_str "_ptr"}.value()) \
        .ctor<>() \
        .custom<type_info>(type_info{ \
        .type_class = TYPE_CLASS_RESOURCE_PTR, \
        .data = {.resource_ptr = { \
            .resource_type_id = entt::hashed_string{name_str}.value(), \
            .get_ptr = +[](const entt::meta_any& a) -> std::shared_ptr<nb::resource> { \
            auto* p = a.try_cast<std::shared_ptr<T>>(); \
            return p ? *p : nullptr; \
            }, \
            .set_ptr = +[](entt::meta_any& a, std::shared_ptr<nb::resource> p) { \
            if(!a.assign(std::static_pointer_cast<T>(p))) { \
                auto target_type = a.type().info().name(); /*not sure if this is correct btw*/\
                auto source_type = typeid(T).name(); \
                log::warn("[res] resource pointer assignment failed: target='%s' source='%s' resource_type_id='%s'", \
                      target_type.length() ? std::string{target_type}.c_str() : "<unknown>", \
                      source_type ? source_type : "<unknown>", \
                      name_str); \
            } \
            } \
        }} \
        });

        NB_REG_RES_PTR(rtexture,          "rtexture")
        NB_REG_RES_PTR(rsprite,           "rsprite")
        NB_REG_RES_PTR(rscript,           "rscript")
        NB_REG_RES_PTR(rvorbis,           "rvorbis")
        NB_REG_RES_PTR(rwav,              "rwav")
        NB_REG_RES_PTR(ryaml,             "ryaml")
        NB_REG_RES_PTR(retree,            "retree")
        NB_REG_RES_PTR(rparticle_emitter, "rparticle_emitter")
#undef NB_REG_RES_PTR
    }

}
