#pragma once

#include <vector>
#include <string>

namespace nb::util {

    // quick n dirty steal from SO 14265581
    inline std::vector<std::string> str_split(std::string s, std::string delimiter) {
        size_t pos_start = 0, pos_end, delim_len = delimiter.length();
        std::string token;
        std::vector<std::string> res;
    
        while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
            token = s.substr (pos_start, pos_end - pos_start);
            pos_start = pos_end + delim_len;
            res.push_back (token);
        }
    
        res.push_back (s.substr (pos_start));
        return res;
    }

    // Collapse ".." and "." segments in a forward-slash path.
    // e.g. "res/map/../spr/foo.png" -> "res/spr/foo.png"
    inline std::string path_normalize(const std::string& path)
    {
        std::vector<std::string> parts;
        std::string seg;
        for (char c : path)
        {
            if (c == '/')
            {
                if (seg == "..")      { if (!parts.empty()) parts.pop_back(); }
                else if (!seg.empty() && seg != ".") parts.push_back(seg);
                seg.clear();
            }
            else { seg += c; }
        }
        if (seg == "..")      { if (!parts.empty()) parts.pop_back(); }
        else if (!seg.empty() && seg != ".") parts.push_back(seg);

        std::string out;
        for (const auto& p : parts) { if (!out.empty()) out += '/'; out += p; }
        return out;
    }

}