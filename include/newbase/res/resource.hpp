#pragma once

#include <newbase/mixins.hpp>
#include <entt/entt.hpp>

namespace nb {

/**
 * @brief Base resource class. All resource types must inherit from this.
 */
class resource : public nocopy {
public:
    virtual ~resource() = default;
    entt::id_type id()      const { return _id; }
    entt::id_type type_id() const { return _type_id; }

    // convenience: resolve the entt meta type for this resource
    entt::meta_type meta_type() const { return entt::resolve(_type_id); }

protected:
    explicit resource(entt::id_type id = 0, entt::id_type type_id = 0)
        : _id(id), _type_id(type_id) {}

private:
    entt::id_type _id      {0};
    entt::id_type _type_id {0};
};

} // namespace nb
