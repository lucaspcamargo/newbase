#pragma once

#include <string>
#include <memory>
#include <SDL3/SDL_storage.h>
#include <entt/core/ident.hpp>

namespace nb::res_storage {

class storage_interface;

/**
 * @brief Asset handle structure
 * Describes a single asset (file) in a storage interface.
 * Includes its unique id (hash), name, path, size, and the index of the storage interface it belongs to.
 * This is related to, but separate from a resource, despite sharinf the same ids most of the time.
 * This describes data available on the storage, whereas an instance of `resource` is the actual resource data loaded in memory.
 * This expected to be internal to the resource system (and editor?), and not exposed to users directly.
 */
struct asset_handle
{
    entt::id_type id {};
    std::string name {};
    std::string path {};
    std::size_t size {std::string::npos};    // string::npos means unknown size
    int storage_interface_idx {-1};         // index into rmanager's storage interface list, must be filled-in by manager
};

}