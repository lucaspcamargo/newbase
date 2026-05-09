#pragma once
#include <SDL3/SDL.h>

#include <stdbool.h>

#ifdef __cplusplus
#define EXTRNC extern "C"
#else
#define EXTRNC
#endif

EXTRNC bool _nb_engine_init(void **userptr, int argc, char **argv);

EXTRNC bool _nb_engine_step(void *userptr);

EXTRNC bool _nb_engine_event(void *userptr, SDL_Event *event);

EXTRNC void _nb_engine_request_exit(void *userptr);

EXTRNC bool _nb_engine_teardown(void *userptr, SDL_AppResult result);

