#include <newbase/i18n/locale_detect.hpp>

#include <SDL3/SDL_stdinc.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace nb::i18n {

static void push_locale(std::vector<std::string>& out, const std::string& raw)
{
    if (raw.empty() || raw == "C" || raw == "POSIX")
        return;

    // Strip encoding suffix: "pt_BR.UTF-8@euro" -> "pt_BR"
    auto dot = raw.find('.');
    std::string clean = (dot != std::string::npos) ? raw.substr(0, dot) : raw;

    // Also strip modifier if somehow still present after dot strip
    auto at = clean.find('@');
    if (at != std::string::npos)
        clean = clean.substr(0, at);

    if (clean.empty())
        return;

    auto already = [&](const std::string& s) {
        for (auto& e : out) if (e == s) return true;
        return false;
    };

    if (!already(clean))
        out.push_back(clean);

    // Language-only fallback: "pt_BR" -> "pt"
    auto us = clean.find('_');
    if (us != std::string::npos)
    {
        std::string lang = clean.substr(0, us);
        if (!already(lang))
            out.push_back(lang);
    }
}

std::vector<std::string> detect_locales()
{
    std::vector<std::string> result;

#if defined(_WIN32)
    {
        wchar_t buf[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH) > 0)
        {
            char narrow[LOCALE_NAME_MAX_LENGTH] = {};
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, narrow, sizeof(narrow), nullptr, nullptr);
            std::string s = narrow;
            // BCP 47 uses '-' (e.g. "pt-BR"), convert to POSIX '_'
            for (char& c : s) if (c == '-') c = '_';
            push_locale(result, s);
        }
    }
#endif

#if defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"})
    {
        const char* val = SDL_getenv(var);
        if (val && *val)
            push_locale(result, val);
    }
#endif

    if (result.empty())
        result.push_back("en");

    return result;
}

} // namespace nb::i18n
