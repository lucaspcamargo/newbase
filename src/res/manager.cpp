#include <newbase/res/manager.h>

namespace nb{

static rmanager rman_inst;

rmanager& resman() {return rman_inst;}

rmanager::rmanager()
{
    
}

entt::resource<rsprite> get_sprite(entt::id_type id, bool forceload = false)
{
    // TODO use cache 
}

entt::resource<rtexture> get_texture(entt::id_type id, bool forceload = false)
{
    // TODO use cache
}

}
