#pragma once

#include <newbase/res/storage/interface.hpp>
#include <ryml.hpp>
#include <SDL3/SDL_storage.h>
#include <entt/entt.hpp>

namespace nb::res_storage {

class sdl_storage : public storage_interface {
public:
    explicit sdl_storage(ryml::ConstNodeRef cfg, std::string base_location);
    ~sdl_storage();

    /** @brief Whether the storage provider allows for writing */
    bool writable() const override;

    /** @brief Whether the storage provider can be scanned (globbed) */
    bool scannable() const override;

    bool has_index() const override;

    /** @brief Invoked by the resource manager after initialization */
    std::vector<asset_handle> get_handles(bool try_scan, bool use_index) override;

    /** @brief Read all bytes of a resource into a buffer. May change in the future. */
    bool read_all_sync(const asset_handle &hnd, std::vector<char> &dst, bool zero_terminate = false) override;
    
private:
    bool _search_index();
    bool _index_add(std::string path, size_t sz);

    // since this is intimately tied to res_manager, we don't benefit much from a d-pointer
    std::string _base_path;
    SDL_Storage * _storage {nullptr};
    bool _index_found {false};
    std::unordered_map<entt::id_type, asset_handle> _handles {};
};

}