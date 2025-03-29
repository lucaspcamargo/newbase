#pragma once

#include <string>
#include <memory>
#include <SDL3/SDL_storage.h>

namespace nb::res_storage {

class storage_interface;

struct descriptor 
{
    std::string path{};  // TODO should be string_view, owned by storage interface
    std::size_t size{0};
    //std::shared_ptr<storage_interface> storage_ref{nullptr}; TODO use when transitioning to storage interfaces
    SDL_Storage * storage_ref;
};

}