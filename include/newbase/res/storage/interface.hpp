#pragma once

#include <newbase/res/storage/handle.hpp>
#include <newbase/mixins.hpp>
#include <vector>

namespace nb::res_storage {

class storage_interface : public nocopy 
{
public:
virtual ~storage_interface() = default;

/** @brief Whether the storage provider allows for writing */
virtual bool writable() const = 0;

/** @brief Whether the storage provider can be scanned (globbed) */
virtual bool scannable() const = 0;

/** @brief Whether the storage provider has an index of its contents */
virtual bool has_index() const = 0;

/** @brief Invoked by the resource manager after initialization. Returns the list of known resources as handles. */
virtual std::vector<asset_handle> get_handles(bool try_scan, bool use_index) = 0;

/** @brief Read all bytes of a resource into a buffer. May change in the future. */
virtual bool read_all_sync(const asset_handle &hnd, std::vector<char> &dst, bool zero_terminate = false) = 0;

};

}