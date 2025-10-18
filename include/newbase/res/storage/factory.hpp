#pragma once

#include <newbase/res/storage/interface.hpp>
#include <entt/entity/handle.hpp>
#inlude <ryml.h>

namespace nb::res_storage::factory {

bool is_supported(entt::id_type id);
const 
std::shared_ptr<nb::res_storage::interface> build(entt::id_type id, ryml::ConstNodeRef cfg);

}