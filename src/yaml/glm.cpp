#include <newbase/yaml/glm.hpp>

namespace nb {

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

}