#include <newbase/yaml/meta_any.hpp>
#include <ryml_std.hpp>
#include <cstdlib>
#include <string>

namespace nb {

void prop_to_yaml(ryml::NodeRef map, c4::csubstr key, const entt::meta_any& val)
{
    auto child = map.append_child();
    child |= ryml::KEYVAL;
    child.set_key(child.tree()->copy_to_arena(key));

    if (const auto* p = val.try_cast<float>())
        child << *p;
    else if (const auto* p = val.try_cast<bool>())
        child.set_val(c4::to_csubstr(*p ? "true" : "false"));
    else if (const auto* p = val.try_cast<int>())
        child << *p;
    else if (const auto* p = val.try_cast<std::string>())
        child.set_val(child.tree()->copy_to_arena(c4::to_csubstr(*p)));
    else if (const auto* p = val.try_cast<entt::id_type>())
        child << *p;
    // unhandled types are silently skipped (no val set = empty)
}

bool prop_from_yaml(ryml::ConstNodeRef scalar, entt::meta_any& out, const entt::meta_any& hint)
{
    if (!scalar.valid() || !scalar.has_val()) return false;

    if (hint.try_cast<float>())
    {
        float v = 0.f; scalar >> v; out = v; return true;
    }
    if (hint.try_cast<bool>())
    {
        std::string s; scalar >> s;
        out = (s == "true" || s == "1" || s == "yes"); return true;
    }
    if (hint.try_cast<int>())
    {
        int v = 0; scalar >> v; out = v; return true;
    }
    if (hint.try_cast<std::string>())
    {
        std::string s; scalar >> s; out = std::move(s); return true;
    }
    if (hint.try_cast<entt::id_type>())
    {
        std::string s; scalar >> s;
        out = static_cast<entt::id_type>(std::strtoul(s.c_str(), nullptr, 0));
        return true;
    }
    // No usable hint — store as string
    std::string s; scalar >> s; out = std::move(s); return true;
}

} // namespace nb
