#include <newbase/system.h>
#include <newbase/reflection/contexts.h>
#include <newbase/reflection/data.h>
#include <newbase/log.h>
#include <entt/entt.hpp>

using entt::operator""_hs;

// maybe add this within engine again
// since it is now generic

std::shared_ptr<nb::system> nb::system::build(const std::string &id, const void *cfgnode)
{
    
    std::string s = id+"_shared";
    auto hashval = entt::hashed_string{s.c_str()}.value();
    log::info("[system_factory] building %s, via %s (%x)", id.c_str(), s.c_str(), hashval);
    
    entt::meta_type type = entt::resolve(rtti::ctx_systems(), hashval);

    if(type)
    {
        //log::info("%s:%x:%x", std::string(type.info().name()).c_str(), type.info().hash(), type.info().index());
        //entt::type_info inf = entt::type_id<std::shared_ptr<nb::system>>();
        //log::info("%s:%x:%x", std::string(inf.name()).c_str(), inf.hash(), inf.index());
        entt::meta_any built = type.construct();
        if(built.allow_cast<std::shared_ptr<nb::system>>())
            return built.cast<std::shared_ptr<nb::system>>();
        else
        {
            log::error("[system_factory] could not complete instantiation, impossible cast");
            return nullptr;
        }
    }
    else
    {
        log::error("[system_factory] not found!");
        return nullptr;
    }
}
