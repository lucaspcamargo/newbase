#include <newbase/res/yaml.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>

#include <ryml.hpp>

using namespace nb;

bool ryaml::do_load()
{
    log::info("[ryaml] loading: %x", id());
    auto read_success = rman().read_all_sync(id(), data, true);
    if(read_success)
    {
        tree = ryml::parse_in_place(c4::to_substr(data.data()));
        yaml_valid = true;
    }
    else
    {
        log::error("[ryaml] cannot read: %x", id());
        return false;
    }
    return true;
}
