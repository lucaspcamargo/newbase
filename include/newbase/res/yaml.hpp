#pragma once

#include <newbase/res/resource.hpp>
#include <ryml.hpp>
#include <vector>

namespace nb {

class ryaml : public resource
{
public:
    explicit ryaml(entt::id_type id = 0, entt::id_type type_id = entt::hashed_string{"ryaml"}.value())
        : resource(id, type_id) {}

    bool yaml_valid {false};
    std::vector<char> data;
    ryml::Tree tree;

protected:
    bool do_load() override;
};

}
