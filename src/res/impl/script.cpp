#include <newbase/res/script.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>


using namespace nb;


bool rscript::do_load()
{
    log::info("[rscript] loading: %x", id());
    valid = false;
    if(!rman().read_all_sync(id(), raw))
    {
        log::error("[rscript] data loading failed: %x", id());
        return false;
    }

    // TODO allow for loading stored bytecode?
    type = script_type::LUA_SOURCE;
    valid = true;

    auto &handles = rman().handles();
    auto it = handles.find(id());
    if (it != handles.end() && !it->second.path.empty())
        chunkname = it->second.path;
    else
    {
        char buf[18];
        snprintf(buf, sizeof(buf), "%x", id());
        chunkname = buf;
    }

    return true;
}
