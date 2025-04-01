#include <newbase/sdl/engine_hooks.h>
#include <newbase/nb_config.h>
#ifdef NEWBASE_USE_XDG_DATA_DIRS
#include <newbase/utility/xdg.h>
#endif

#define SDL_MAIN_USE_CALLBACKS
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_properties.h"

#include <stdio.h>
#include <newbase/reflection/init.h>


inline static enum SDL_AppResult bool_to_app_result(bool val)
{
	return val? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING, "newbase demo");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING, NEWBASE_VERSION);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_IDENTIFIER_STRING, "br.eng.camargo.newbase.demo");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_CREATOR_STRING, NEWBASE_AUTHORS);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_COPYRIGHT_STRING, NEWBASE_COPYRIGHT);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, NEWBASE_URL);
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "game");

#ifdef NEWBASE_USE_XDG_DATA_DIRS
    _nb_xdg_data_dirname_set("newbase_demo");
#endif

    _rtti_init_newbase();

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
