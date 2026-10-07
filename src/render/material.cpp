#include <newbase/render/material.hpp>
#include <newbase/render/mat/pbr.hpp>
#include <unordered_map>

namespace nb::render
{

static auto& _mat_ctrl_map()
{
    static auto map = []() -> auto {
        std::unordered_map<material_type, std::unique_ptr<material_controller>> map;
        map.emplace(material_type::STANDARD_PBR, std::make_unique<pbr_mat_controller>());
        return map;
    }();
    return map;
}

material_controller* material_controller::get_controller(material_type mt)
{
    const auto &map = _mat_ctrl_map();

    auto it = map.find(mt);
    if(it != map.end())
        return it->second.get();

    return nullptr;
}

void material_controller::release_all_controllers()
{
    _mat_ctrl_map().clear();
}

}
