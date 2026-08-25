#include <newbase/sdl/engine_hooks.h>
#include <newbase/nb_config.h>
#include <newbase/reflection/init.h>

void _nb_demo_register_systems(void);
#ifdef NEWBASE_USE_XDG_DATA_DIRS
#include <newbase/utility/xdg.h>
#endif
#ifdef NEWBASE_SGDK
#include <newbase/sgdk/api/sys.h>
extern void nb_sgdk_main(bool);  // game entry point, renamed via -Dmain=nb_sgdk_main
#endif

#define SDL_MAIN_USE_CALLBACKS
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_hints.h"
#include "SDL3/SDL_properties.h"

#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>
#endif

#include <stdio.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Query-string command-line arguments use repeated `arg` parameters, for
// example: newbase_demo.html?arg=--demo&arg=5. URLSearchParams performs the
// browser's standard percent-decoding before handing each token to C.
EM_JS(int, nb_query_arg_count, (), {
    return new URLSearchParams(window.location.search).getAll("arg").length;
});

EM_JS(char *, nb_query_arg, (int index), {
    var values = new URLSearchParams(window.location.search).getAll("arg");
    return index >= 0 && index < values.length ? stringToNewUTF8(values[index]) : 0;
});

static char **nb_emscripten_argv(int argc, char **argv, int *out_argc)
{
    int extra_count = nb_query_arg_count();
    if (extra_count <= 0) {
        *out_argc = argc;
        return argv;
    }

    char **result = malloc((size_t)(argc + extra_count + 1) * sizeof(*result));
    if (!result) {
        *out_argc = argc;
        return argv;
    }

    for (int i = 0; i < argc; ++i)
        result[i] = argv[i];
    for (int i = 0; i < extra_count; ++i)
        result[argc + i] = nb_query_arg(i);
    result[argc + extra_count] = NULL;
    *out_argc = argc + extra_count;
    return result;
}
#endif


inline static enum SDL_AppResult bool_to_app_result(bool val)
{
	return val? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
#ifdef TRACY_ENABLE
    TracyCIsConnected;
#endif

    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING, "newbase demo");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING, NEWBASE_VERSION);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING, "br.eng.camargo.newbase.demo");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_CREATOR_STRING, NEWBASE_AUTHORS);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_COPYRIGHT_STRING, NEWBASE_COPYRIGHT);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, NEWBASE_URL);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "game");

#ifdef NEWBASE_USE_XDG_DATA_DIRS
    _nb_xdg_data_dirname_search("newbase_demo");
#endif

#ifdef __EMSCRIPTEN__
    // restrict keyboard capture to the canvas element only
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
    // use requestAnimationFrame (interval=0) for vsync-synced main loop
    SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "0");
#endif

    _rtti_init_newbase();

#ifdef __EMSCRIPTEN__
    int effective_argc = argc;
    char **effective_argv = nb_emscripten_argv(argc, argv, &effective_argc);
#else
    int effective_argc = argc;
    char **effective_argv = argv;
#endif

#ifdef NEWBASE_SGDK
    nb_sgdk_set_main(nb_sgdk_main);
#endif

	(*appstate) = NULL;
    if (!_nb_engine_init(appstate, effective_argc, effective_argv))
		return SDL_APP_FAILURE;
	_nb_demo_register_systems();
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
	return bool_to_app_result(_nb_engine_step(appstate));
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
	return bool_to_app_result(_nb_engine_event(appstate, event));
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
	_nb_engine_teardown(appstate, result);
}
