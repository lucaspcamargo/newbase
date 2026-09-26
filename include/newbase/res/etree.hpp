#pragma once

#include <newbase/res/yaml.hpp>

namespace nb {

class retree : public ryaml
{
public:
    explicit retree(entt::id_type id = 0) : ryaml(id, entt::hashed_string{"retree"}.value()) {}

    bool etree_valid {false};

protected:
    bool do_load() override;
};

}
