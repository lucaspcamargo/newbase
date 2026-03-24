#pragma once

// Emscripten-specific utilities.
// Safe to include on all platforms — everything is guarded by __EMSCRIPTEN__.

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

namespace nb::ems {

// Flush in-memory filesystem writes back to IndexedDB (IDBFS).
// Call this after any operation that writes to SDL_GetPrefPath.
// The sync is asynchronous; errors are logged to the browser console.
inline void sync_pref_path() {
    EM_ASM(
        FS.syncfs(false, function (err) {
            if (err) console.warn('[nb] IDBFS save error:', err);
        });
    );
}

} // namespace nb::ems

#endif // __EMSCRIPTEN__
