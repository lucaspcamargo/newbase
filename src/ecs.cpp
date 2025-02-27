#include <newbase/ecs.h>

#include <entt/entt.hpp>

static entt::registry _reg;

entt::registry &nb::reg()
{
    return _reg;
}