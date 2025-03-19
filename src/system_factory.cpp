#include <newbase/system.h>
#include <newbase/render_simple/render_simple.h>
#include <newbase/audio/audio.h>
#include <newbase/script_lua/script_lua.h>

std::shared_ptr<nb::system> nb::system::build(const std::string &id, const void *cfgnode)
{
    if(id == "render_simple")
    {
        return std::make_shared<nb::render_simple>();
    }
    else if(id == "audio")
    {
        return std::make_shared<nb::audio>();
    }
    else if(id == "script_lua")
    {
        return std::make_shared<nb::script_lua>();
    }

    return nullptr;
}
