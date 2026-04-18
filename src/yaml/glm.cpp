#include <newbase/yaml/glm.hpp>
#include <string>

namespace nb {

    bool load_vec2(ryml::ConstNodeRef in, glm::vec2 &dst)
    {
        assert(in.is_seq());
        assert(in.num_children() == 2);
        in.at(0) >> dst[0];
        in.at(1) >> dst[1];
        return true;
    }

    bool load_vec3(ryml::ConstNodeRef in, glm::vec3 &dst)
    {
        assert(in.is_seq());
        assert(in.num_children() == 3);
        in.at(0) >> dst[0];
        in.at(1) >> dst[1];
        in.at(2) >> dst[2];
        return true;
    }

    bool load_vec4(ryml::ConstNodeRef in, glm::vec4 &dst)
    {
        assert(in.is_seq());
        assert(in.num_children() == 4);
        in.at(0) >> dst[0];
        in.at(1) >> dst[1];
        in.at(2) >> dst[2];
        in.at(3) >> dst[3];
        return true;
    }

    bool try_load_float(ryml::ConstNodeRef in, float &dst)
    {
        if (!in.valid() || !in.has_val()) return false;
        in >> dst;
        return true;
    }

    bool try_load_bool(ryml::ConstNodeRef in, bool &dst)
    {
        if (!in.valid() || !in.has_val()) return false;
        std::string s;
        in >> s;
        dst = (s == "true" || s == "1" || s == "yes");
        return true;
    }

    bool try_load_vec2(ryml::ConstNodeRef in, glm::vec2 &dst)
    {
        if (!in.valid() || !in.is_seq() || in.num_children() < 2) return false;
        return load_vec2(in, dst);
    }

    bool try_load_vec4(ryml::ConstNodeRef in, glm::vec4 &dst)
    {
        if (!in.valid() || !in.is_seq() || in.num_children() < 4) return false;
        return load_vec4(in, dst);
    }

}
