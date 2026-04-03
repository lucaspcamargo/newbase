#pragma once

#include <string>

namespace nb {

// Read-only text viewer widget.
// Call open() once with the raw text, then draw() each frame.
class text_editor_widget
{
public:
    text_editor_widget() = default;

    void open(const char* text, size_t len, const char* language = nullptr);
    void draw();

private:
    std::string _text;
    const char* _language {nullptr}; // display hint only (e.g. "Lua", "YAML")
};

} // namespace nb
