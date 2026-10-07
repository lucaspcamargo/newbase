#pragma once

#include <newbase/res/yaml.hpp>

namespace nb {

class retree : public ryaml
{
public:
    explicit retree(entt::id_type id = 0) : ryaml(id, entt::hashed_string{"retree"}.value()) {}

    bool etree_valid {false};


    const std::vector<dependency_t>*  dependencies() const override { return &m_deps; }

protected:
    bool do_preload() override;
    bool do_load() override;

private:
    std::vector<dependency_t> m_deps;

};

}
