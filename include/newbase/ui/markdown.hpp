#pragma once
#include <string_view>

namespace nb::ui {

// Render a markdown string inline in the current ImGui window.
void Markdown(std::string_view text);

// Convenience: render a null-terminated markdown string.
inline void Markdown(const char* text) { Markdown(std::string_view{text}); }

} // namespace nb::ui
