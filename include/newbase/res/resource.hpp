#pragma once

#include <newbase/mixins.hpp>
#include <entt/meta/meta.hpp>
#include <entt/resource/resource.hpp>
#include <memory>

namespace nb {

namespace res_storage {
    struct asset_handle;
}

/**
 * @brief Base resource class
 * This is meant to be subclassed by the different resource types.
 * Every resource MUST have a unique id (hash) and a concrete type id (entt type id).
 * Every resource may have a (weak) reference to the storage asset handle.
 * For now, any instance of resource is loaded in memory.
 * However, in the future we may have lazy-loaded resources that load data on demand (e.g. texture mip levels).
 * If we do that, a common interface at this level could be useful.
 */
class resource : public nocopy 
{
    virtual ~resource() = default;

protected:
    /**
     * @brief Initialize base data for resources
     * @param id Resource unique id (hash), should match asset descriptor id if from storage
     * @param type_id Concrete resource type id (entt type id), used for RTTI
     * @param asset_handle Weak pointer to asset handle describing this resource's storage info. Can be null.
     */
    resource(entt::id_type id, entt::id_type type_id, std::weak_ptr<res_storage::asset_handle> asset_handle = std::weak_ptr<res_storage::asset_handle>{})
        : _id(id)
        , _concrete_type(type_id)
        , _asset_handle(std::move(asset_handle))
    {}

    entt::id_type id() const { return _id; }

    /**
     * @brief Get a pointer to the asset handle, if any, describing this resource's storage info.
     * @return Shared pointer to asset handle, or nullptr if none available.
     */
    std::shared_ptr<res_storage::asset_handle> asset_handle() const { return _asset_handle.lock(); }

private:
    entt::id_type _id {0};
    entt::id_type _concrete_type {0};
    std::weak_ptr<res_storage::asset_handle> _asset_handle {};
};


/**
 * @brief Resource handle class
 * For now we use the entt implementation as a base.
 * We may want to make this a template later if we need more control.
 */
template<typename ResType = resource>
class res_ref final : public entt::resource<ResType> 
{
public:
    using entt::resource<ResType>::resource; // inherit constructor
};

} // namespace nb