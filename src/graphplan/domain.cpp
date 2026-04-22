#include <newbase/graphplan/domain_registry.hpp>
#include <unordered_map>
#include <string>

namespace nb::graphplan {

static std::unordered_map<std::string, const domain*> s_domains;

void register_domain(const domain& dom)
{
    s_domains[dom.id] = &dom;
}

const domain* find_domain(const char* id)
{
    auto it = s_domains.find(id);
    return it != s_domains.end() ? it->second : nullptr;
}

} // namespace nb::graphplan
