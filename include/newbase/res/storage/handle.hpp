#pragma once

#include <entt/core/ident.hpp>
#include <SDL3/SDL_storage.h>
#include <string>

namespace nb::res_storage {

class storage_interface;

/**
 * @brief Asset handle structure
 * Describes a single asset (file) in a storage interface.
 * Includes its unique id (hash), name, path, size, and the index of the storage interface it belongs to.
 * This is related to, but separate from a resource, despite sharing the same ids most of the time.
 * This describes data available on the storage, whereas an instance of `resource` is the actual resource data loaded in memory.
 * This expected to be internal to the resource system (and editor?), and not exposed to users directly.
 * One wrinkle to asset_handle is the management of subresources. They are part of another resource and
 * cannot be used to read data directly. Instead, the resource must obtain its parent resource and get its
 * data from it.
 */
struct asset_handle
{
    entt::id_type id {};                     // Id of the resource. Always a hash of the full resource path.
    std::string name {};                     // Base filename, or query if handle is a subresource.
    std::string path {};                     // Full path to the resource, including query params for subresources.
    std::size_t size {std::string::npos};    // string::npos means unknown size. Subresources have no file data, so always npos.
    int storage_interface_idx {-1};          // index into rmanager's storage interface list, must be filled-in by manager
    entt::id_type parent_id {0};             // if not zero, resource is a subresource
};

}
