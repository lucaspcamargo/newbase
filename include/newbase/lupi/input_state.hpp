#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace nb {

static constexpr int LUPI_BUTTON_COUNT = 16; // ids 0-15 (LEFT..BTN_E; 6-11 reserved/unbound)
static constexpr int LUPI_MAX_PLAYERS  = 3;  // only player 0 is bound to real keys in this MVP

// pressure 0-255; keyboard buttons in this MVP report 0 or 255 (fully digital).
struct lupi_button_state {
    std::array<std::array<uint8_t, LUPI_BUTTON_COUNT>, LUPI_MAX_PLAYERS> pressure_this_frame {};
    std::array<std::array<uint8_t, LUPI_BUTTON_COUNT>, LUPI_MAX_PLAYERS> pressure_last_frame {};
};

struct lupi_mouse_state {
    float x = 0, y = 0;
    uint32_t buttons = 0; // bitfield, bit N = SDL button N held
    // wheel_x/wheel_y always report 0 — unsupported per spec
};

// simple FIFO of pending UTF-8 bytes fed by SDL_EVENT_TEXT_INPUT
struct lupi_text_queue {
    std::string ring;

    void push(const char* utf8) { ring += utf8; }

    // peek/consume one byte at a time (spec describes single-character semantics;
    // multi-byte UTF-8 sequences are delivered byte-by-byte across successive calls)
    bool peek(char& out) const { if (ring.empty()) return false; out = ring.front(); return true; }
    bool read(char& out) { if (ring.empty()) return false; out = ring.front(); ring.erase(ring.begin()); return true; }
};

}
