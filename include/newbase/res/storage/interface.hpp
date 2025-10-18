#pragma once

namespace nb::res_storage {

class storage_interface 
{
public:
/** @brief Whether the storage provider allows for writing */
virtual bool writable() const = 0;

/** @brief Whether the storage provider can be scanned (globbed) */
virtual bool scannable() const = 0;



};

}