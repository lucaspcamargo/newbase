#pragma once

#include <SDL3/SDL_rect.h>

// Some useful operators for operating on SDL_Rect and SDL_FRect

inline bool operator ==(const SDL_FRect &lhs, const SDL_FRect &rhs)
{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
}

inline bool operator ==(const SDL_Rect &lhs, const SDL_Rect &rhs)
{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.w == rhs.w && lhs.h == rhs.h;
}
