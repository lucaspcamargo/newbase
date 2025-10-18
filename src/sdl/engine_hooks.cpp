#include <newbase/sdl/engine_hooks.h>
#include <newbase/engine.hpp>

#include <cassert>

EXTRNC bool _nb_engine_init(void **userptr, int argc, char **argv)
{
    assert(userptr);
    auto &eng = nb::engine::instance();
    (*userptr) = &eng;
    return eng.init(argc, argv);
}

EXTRNC bool _nb_engine_step(void *userptr)
{
    nb::engine *eng = reinterpret_cast<nb::engine*>(userptr);
    return eng->step();
}

EXTRNC bool _nb_engine_event(void *userptr, SDL_Event *event)
{
    nb::engine *eng = reinterpret_cast<nb::engine*>(userptr);
    return eng->event(event);
}

EXTRNC bool _nb_engine_teardown(void *userptr, SDL_AppResult result)
{
    nb::engine *eng = reinterpret_cast<nb::engine*>(userptr);
    eng->teardown();
    return true;
}
