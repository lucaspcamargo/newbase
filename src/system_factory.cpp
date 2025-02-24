#include <newbase/system.h>
#include <newbase/render_simple/render_simple.h>
#include <newbase/audio/audio.h>

std::shared_ptr<nb::system> nb::system::build(const std::string &id, __attribute_maybe_unused__ const void *cfgnode)
{
    if(id == "render_simple")
    {
        return std::make_shared<nb::render_simple>();
    }
    else if(id == "audio")
    {
        return std::make_shared<nb::audio>();
    }

    return nullptr;
}
