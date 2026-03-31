#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Register the game entry point. Must be called before the engine starts
// (e.g. in SDL_AppInit, before _nb_engine_init). The sgdk system will call
// fn() from its dedicated game thread. Typically fn is the game's main(),
// renamed to nb_sgdk_main via -Dmain=nb_sgdk_main at compile time.
void nb_sgdk_set_main(void (*fn)(bool));

// Called by the host when a clean shutdown is needed.
// Issues a longjmp that unwinds the entire game call stack.
void nb_sgdk_request_exit(void);

#ifdef __cplusplus
} // extern "C"
#endif
