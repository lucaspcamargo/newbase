#include <newbase/i18n/i18n.hpp>
#include <newbase/i18n/locale_detect.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/vfs.hpp>
#include <newbase/res/storage/handle.hpp>
#include <newbase/log.hpp>

#include <tinygettext/file_system.hpp>
#include <tinygettext/dictionary_manager.hpp>
#include <tinygettext/language.hpp>

#include <entt/core/hashed_string.hpp>
#include <ryml_std.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace nb::i18n {

// Walk the VFS tree to the node at the given slash-separated path.
static entt::entity vfs_find(const std::string& path)
{
    const auto& vfs = rman().vfs();
    const auto& reg = vfs.registry();

    entt::entity cur = vfs.root();
    size_t start = 0;
    while (start < path.size())
    {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        if (end == start) { start++; continue; } // skip leading/double slashes

        std::string seg = path.substr(start, end - start);
        start = end + 1;

        const vfs_node* node = reg.try_get<vfs_node>(cur);
        if (!node) return entt::null;

        bool found = false;
        for (entt::entity child : node->children)
        {
            const vfs_node* cn = reg.try_get<vfs_node>(child);
            if (cn && cn->name == seg)
            {
                cur = child;
                found = true;
                break;
            }
        }
        if (!found) return entt::null;
    }
    return cur;
}

struct NbFileSystem : tinygettext::FileSystem
{
    // Returns the bare filenames (not full paths) of assets under pathname.
    std::vector<std::string> open_directory(const std::string& pathname) override
    {
        std::vector<std::string> result;
        entt::entity dir = vfs_find(pathname);
        if (dir == entt::null) return result;

        const auto& reg = rman().vfs().registry();
        const vfs_node* node = reg.try_get<vfs_node>(dir);
        if (!node) return result;

        for (entt::entity child : node->children)
        {
            if (reg.all_of<res_storage::asset_handle>(child))
            {
                const vfs_node* cn = reg.try_get<vfs_node>(child);
                if (cn) result.push_back(cn->name);
            }
        }
        return result;
    }

    // filename is the full asset path, e.g. "locale/pt_BR.po"
    std::unique_ptr<std::istream> open_file(const std::string& filename) override
    {
        auto id = entt::hashed_string{filename.c_str()}.value();
        std::vector<char> data;
        if (!rman().read_all_sync(id, data, false))
        {
            log::error("[i18n] failed to open file: %s", filename.c_str());
            return nullptr;
        }
        return std::make_unique<std::istringstream>(std::string{data.data(), data.size()});
    }
};

static std::unique_ptr<tinygettext::DictionaryManager> s_mgr;
static std::string s_language;

void init(ryml::ConstNodeRef root_cfg)
{
    std::string locale_dir = "locale";
    std::string forced_lang;

    if (root_cfg.readable() && root_cfg.is_map() && root_cfg.has_child("i18n"))
    {
        auto cfg = root_cfg["i18n"];
        if (cfg.has_child("locale_dir"))
        {
            c4::from_chars(cfg["locale_dir"].val(), &locale_dir);
        }
        if (cfg.has_child("language"))
        {
            c4::from_chars(cfg["language"].val(), &forced_lang);
            if (forced_lang == "auto") forced_lang.clear();
        }
    }

    s_mgr = std::make_unique<tinygettext::DictionaryManager>(
        std::make_unique<NbFileSystem>());
    if (!s_mgr)
    {
        log::error("[i18n] failed to create DictionaryManager");
        return;
    }
    s_mgr->add_directory(locale_dir);

    std::string lang;
    if (!forced_lang.empty())
    {
        lang = forced_lang;
    }
    else
    {
        auto candidates = detect_locales();
        auto available = s_mgr->get_languages();
        for (const auto& candidate : candidates)
        {
            tinygettext::Language l = tinygettext::Language::from_env(candidate);
            for (const auto& avail : available)
            {
                if (tinygettext::Language::match(l, avail) > 0)
                {
                    lang = candidate;
                    break;
                }
            }
            if (!lang.empty()) break;
        }
        // No matching .po found — pick first system locale and let tinygettext use empty dict
        if (lang.empty() && !candidates.empty())
            lang = candidates.front();
    }

    if (!lang.empty())
    {
        s_mgr->set_language(tinygettext::Language::from_env(lang));
        s_language = lang;
        log::info("[i18n] language: %s (locale_dir: %s)", lang.c_str(), locale_dir.c_str());
    }
    else
    {
        log::info("[i18n] no language set, using empty dictionary (locale_dir: %s)", locale_dir.c_str());
    }
}

void shutdown()
{
    s_mgr.reset();
    s_language.clear();
}

void set_language(const std::string& lang)
{
    s_language = lang;
    if (s_mgr)
        s_mgr->set_language(tinygettext::Language::from_env(lang));
}

std::string language()
{
    return s_language;
}

std::string tr(const std::string& msg)
{
    if (!s_mgr) return msg;
    return s_mgr->get_dictionary().translate(msg);
}

std::string ntr(const std::string& msg, const std::string& msg_plural, int n)
{
    if (!s_mgr) return (n == 1) ? msg : msg_plural;
    return s_mgr->get_dictionary().translate_plural(msg, msg_plural, n);
}

} // namespace nb::i18n
