#pragma once
#include <string>
#include <vector>

namespace nb::i18n {

// Returns preferred locale strings ordered by priority, most specific first.
// E.g. {"pt_BR", "pt", "en"} or {"en"} as fallback.
// Encoding suffixes (e.g. ".UTF-8") are stripped; country-only fallbacks are appended.
std::vector<std::string> detect_locales();

} // namespace nb::i18n
