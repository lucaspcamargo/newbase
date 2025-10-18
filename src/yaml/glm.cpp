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

}