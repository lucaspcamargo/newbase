#include <newbase/res/rtti.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/reflection/resources.hpp>
#include <newbase/res/writers.hpp>
#include <newbase/res/etree.hpp>
#include <newbase/res/gltf.hpp>
#include <newbase/res/graphplan.hpp>
#include <newbase/res/material.hpp>
#include <newbase/res/mesh.hpp>
#include <newbase/res/script.hpp>
#include <newbase/res/shader.hpp>
#include <newbase/res/sprite.hpp>
#include <newbase/res/texture.hpp>
#include <newbase/res/tilemap.hpp>
#include <newbase/res/vorbis.hpp>
#include <newbase/res/wav.hpp>
#include <newbase/res/yaml.hpp>
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
            .base<resource>()
            .custom<type_info>(type_info{
                .identifier = "script",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_CODE_O, .extensions = "lua",
                    .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                        return std::make_shared<rscript>(id);
                        }
                    }}
                });

        entt::meta_factory<rtexture>{}
        .type("rtexture"_hs)
        .base<resource>()
            .custom<type_info>(type_info{
                .identifier = "texture",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_IMAGE_O, .extensions = "png jpg jpeg bmp",
                    .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                        return std::make_shared<rtexture>(id);
                    }
                }},
                .saver_fn = rwriter_texture,
            })
        .data<&rtexture::nearest, entt::as_ref_t>("nearest"_hs)
        .custom<rtti::data_info>(rtti::data_info{"nearest"});


        entt::meta_factory<rshader>{}
        .type("rshader"_hs)
        .base<resource>()
        .custom<type_info>(type_info{
            .identifier = "shader",
            .type_class = TYPE_CLASS_RESOURCE,
            .data = {.resource = {.editor_icon = ICON_FK_MAGIC, .extensions = "slang hlsl spv nbs",
                .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                    return std::make_shared<rshader>(id);
                }
            }}
        });


        entt::meta_factory<rsprite>{}
        .type("rsprite"_hs)
        .base<resource>()
            .custom<type_info>(type_info{
                .identifier = "sprite",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_IMAGE_O, .extensions = "sprite",
                    .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                        return std::make_shared<rsprite>(id);
                    }
                }},
            })
            .ctor<>()
            .data<&rsprite::anchor, entt::as_ref_t>("anchor"_hs)
                .custom<rtti::data_info>(rtti::data_info{"anchor"})
            .data<&rsprite::dims, entt::as_ref_t>("dims"_hs)
                .custom<rtti::data_info>(rtti::data_info{"dims"})
            .data<&rsprite::tex, entt::as_ref_t>("tex"_hs)
                .custom<rtti::data_info>(rtti::data_info{"tex"});


        entt::meta_factory<rvorbis>{}
        .type("rvorbis"_hs)
        .base<resource>()
            .custom<type_info>(type_info{
                .identifier = "vorbis",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_AUDIO_O, .extensions = "ogg",
                    .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                        return std::make_shared<rvorbis>(id);
                    }
                }}
            });

        entt::meta_factory<rwav>{}
        .type("rwav"_hs)
        .base<resource>()
            .custom<type_info>(type_info{
                .identifier = "wav",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_AUDIO_O, .extensions = "wav",
                    .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                        return std::make_shared<rwav>(id);
                    }
                }}
            });

        entt::meta_factory<ryaml>{}
        .type("ryaml"_hs)
        .base<resource>()
            .custom<type_info>(type_info{
                .identifier = "yaml",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_TEXT_O, .extensions = "yaml yml",
                    .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                        return std::make_shared<ryaml>(id);
                    }
                }}
            });

        entt::meta_factory<retree>{}
        .type("retree"_hs)
        .base<resource>()
            .custom<type_info>(type_info{
                .identifier = "etree",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_FILE_TEXT_O, .extensions = "etree",
                    .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                        return std::make_shared<retree>(id);
                    }
                }}
            });

        entt::meta_factory<rtilemap>{}
        .type("rtilemap"_hs)
        .base<resource>()
            .custom<type_info>(type_info{
                .identifier = "tilemap",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_MAP, .extensions = "tmj",
                    .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                        return std::make_shared<rtilemap>(id);
                    }
                }}
            });

        entt::meta_factory<rgraphplan>{}
        .type("rgraphplan"_hs)
        .base<resource>()
            .custom<type_info>(type_info{
                .identifier = "graphplan",
                .type_class = TYPE_CLASS_RESOURCE,
                .data = {.resource = {.editor_icon = ICON_FK_SITEMAP, .extensions = "graphplan",
                    .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                        return std::make_shared<rgraphplan>(id);
                    }
                }}
            });

        // shared_ptr<T> registrations — used by meta_any_editor to display resource fields
        res_ptr_registration<retree>("retree");
        res_ptr_registration<rgraphplan>("rgraphplan");
        res_ptr_registration<rtilemap>("rtilemap");
        res_ptr_registration<rtexture>("rtexture");
        res_ptr_registration<rshader>("rshader");
        res_ptr_registration<rsprite>("rsprite");
        res_ptr_registration<rscript>("rscript");
        res_ptr_registration<rvorbis>("rvorbis");
        res_ptr_registration<rwav>("rwav");
        res_ptr_registration<ryaml>("ryaml");


        // in-type rtti registrations
        rgltf::_init_rtti();
        rmaterial::_init_rtti();
        rmesh::_init_rtti();
    }

}
