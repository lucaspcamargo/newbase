#include <newbase/sdl/engine_hooks.h>

#define SDL_MAIN_USE_CALLBACKS
#include "SDL3/SDL_main.h"

#include <stdio.h>


inline static enum SDL_AppResult bool_to_app_result(bool val)
{
	return val? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
	(*appstate) = NULL;
	return bool_to_app_result(_nb_engine_init(appstate, argc, argv));
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
