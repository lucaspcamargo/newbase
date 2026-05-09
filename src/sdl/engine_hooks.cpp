#include <newbase/sdl/engine_hooks.h>
#include <newbase/engine.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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

EXTRNC
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void _nb_engine_request_exit(void *userptr)
{
    nb::engine *eng = reinterpret_cast<nb::engine*>(userptr);
    eng->request_exit();
}

EXTRNC bool _nb_engine_teardown(void *userptr, SDL_AppResult result)
{
    nb::engine *eng = reinterpret_cast<nb::engine*>(userptr);
    eng->teardown();
    return true;
}
