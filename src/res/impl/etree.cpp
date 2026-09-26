#include <newbase/res/etree.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/log.hpp>

#include <ryml.hpp>

using namespace nb;

// TODO
// we want to change approach for etree, like tell ryaml that we actually
// need the yaml data during preload
// some part of builders will actually need to live or be called from here
// as we will need to understand which resources are needed during the preload stage
// this is deeply related with scene IO, we need to model that properly
// perhaps we replace this approach with a new "rscene" type altogether


bool retree::do_load()
{
    log::info("[retree] loading: 0x%08x", id());

    if(!ryaml::do_load())
    {
        log::info("[retree] base yaml loading failed: 0x%08x", id());
        return false;
    }

    // TODO parse to specific etree data here
    etree_valid = yaml_valid;

    log::info("[retree] loaded: 0x%08x", id());

    return true;
}
