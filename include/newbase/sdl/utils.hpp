#pragma once

#include <vector>
#include <string>
#include <functional>

namespace nb {

inline std::vector<std::string> get_all_strings(std::function<int(void)> countfn, std::function<const char*(int)> getfn)
{
    const int count = countfn();
    std::vector<std::string> ret;
    for(int i = 0; i < count; i++)
        ret.emplace_back(std::string{getfn(i)});
    return ret;
}

inline std::string join_strings(const std::vector<std::string> &in, char sep)
{
    std::string ret;
    for(const auto &s: in)
    {
        ret += s;
        ret += sep;
    }
    return ret.size()? ret.substr(0, ret.size()-1) : ret;
}

}
