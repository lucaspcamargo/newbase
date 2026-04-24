#pragma once
#include <string>
#include <ryml.hpp>

namespace nb::i18n {

// Called from engine init after the resource manager is configured.
// Reads optional "i18n" key from the root config for locale_dir and language overrides.
void init(ryml::ConstNodeRef root_cfg);
void shutdown();

// Override the active language (e.g. "pt_BR", "en"). Triggers a dictionary reload.
void set_language(const std::string& lang);

// Returns the currently active language string, or empty string if not set.
std::string language();

// Translate a message using the active dictionary.
// Returns the original message if no translation is found.
std::string tr(const std::string& msg);

// Translate a plural-form message.
std::string ntr(const std::string& msg, const std::string& msg_plural, int n);

} // namespace nb::i18n
