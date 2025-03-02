#include <newbase/res/loaders.h>
#include <newbase/res/manager.h>
#include <newbase/res/etree.h>
#include <newbase/res/texture.h>
#include <newbase/res/sprite.h>

namespace nb {

    // etree loader
    rloader_etree::result_type rloader_etree::operator()(entt::id_type) const
    {
        return std::make_shared<retree>();
    }

    // sprite loader
    rloader_sprite::result_type rloader_sprite::operator()(entt::id_type) const
    {
        return std::make_shared<rsprite>();
    }

    // texture loader
    rloader_texture::result_type rloader_texture::operator()(entt::id_type) const
    {
        return std::make_shared<rtexture>();
    }

}