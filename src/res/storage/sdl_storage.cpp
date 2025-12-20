#include <newbase/res/storage/sdl_storage.hpp>
#include <newbase/utility/strings.hpp>
#include <newbase/nb_config.h>
#include <newbase/log.hpp>

namespace nb::res_storage {

sdl_storage::sdl_storage(ryml::ConstNodeRef cfg, std::string base_location)
{
    (void) cfg;

    _storage = SDL_OpenTitleStorage(base_location.c_str(), SDL_CreateProperties());
    if(!_storage)
    {
        log::warn("[rmanager] cannot open title storage. Falling back to raw fs...");
        _storage = SDL_OpenFileStorage(NEWBASE_DEFAULT_RES_PREFIX);
    }

    _search_index();
}

sdl_storage::~sdl_storage()
{
    if(_storage)
    {
        SDL_CloseStorage(_storage);
    }
}

bool sdl_storage::writable() const
{
    return false;  // user storage will come later
}

bool sdl_storage::scannable() const
{
    return _storage && true;
}

bool sdl_storage::has_index() const
{
    return _storage && _index_found;
}


std::vector<asset_handle> sdl_storage::get_handles(bool try_scan, bool use_index)
{
    if(!_storage)
    {
        return {};
    }
    
    std::vector<asset_handle> result;

    if(use_index && !_index_found)
    {
        _index_found = _search_index();
    }

    if(_index_found)
    {
        for(const auto &pair : _handles)
        {
            result.push_back(pair.second);
        }
    }
    else if(try_scan)
    {
        char ** vfiles = SDL_GlobStorageDirectory(_storage, nullptr, nullptr, 0, nullptr);
        if(vfiles)
        {
            for(char **curr = vfiles; *curr; curr++)
            {
                Uint64 sz;
                if(SDL_GetStorageFileSize(_storage, *curr, &sz))
                {
                    asset_handle handle;
                    std::string path {*curr};
                    bool absolute = path[0] == '/';
                    auto hash = entt::hashed_string(absolute? path.c_str()+1 : path.c_str());
                    handle.id = hash.value();
                    handle.name = ::nb::util::str_split(path, "/").back();
                    handle.path = path;
                    handle.size = static_cast<size_t>(sz);
                    handle.storage_interface_idx = -1;
                    result.push_back(handle);
                }
            }
            SDL_free(vfiles);
        }
        else
        {
            log::error("[sdl_storage] cannot scan storage directory!");
        }
    }

    return result;
}


bool sdl_storage::_search_index()
{
    std::string idx_path {"index.yaml"};
    Uint64 idx_len;
    std::vector<char> buf;
    
    if (SDL_GetStorageFileSize(_storage, idx_path.c_str(), &idx_len) && idx_len > 0)
    {
        buf.resize(static_cast<size_t>(idx_len+1));
        buf.back() = '\0';
        if (SDL_ReadStorageFile(_storage, idx_path.c_str(), buf.data(), idx_len))
        {
            auto tree = ryml::parse_in_place(buf.data());
            assert(tree.rootref().has_children());
            for(ryml::ConstNodeRef n : tree.rootref().cchildren())
            {
                std::string path;
                size_t sz;
                n[0] >> path;
                n[1] >> sz;
                _index_add(path.c_str(), sz);
            }
            return true;
        }
        else 
        {
            log::error("[sdl_storage] cannot open index.yaml!");
        }
    }
    else
    {
        log::error("[sdl_storage] cannot stat index.yaml!");
    }
    
    return false;
}

bool sdl_storage::_index_add(std::string path, size_t sz)
{
    if(path.empty() || path == "/")
        return false;
    bool absolute = path[0] == '/';
    auto hash = entt::hashed_string(absolute? path.c_str()+1 : path.c_str());
    log::info("[sdl_storage] registered: %s (%x)", path.c_str(), hash.value());
    _handles.insert(std::make_pair(hash.value(), 
        asset_handle{
            hash.value(), 
            ::nb::util::str_split(path, "/").back(), 
            path, 
            sz, 
            -1}));
    return true;
}


bool sdl_storage::read_all_sync(const asset_handle &hnd, std::vector<char> &dst, bool zero_terminate)
{
    if(!_storage)
    {
        return false;
    }

    Uint64 sz;
    if(!SDL_GetStorageFileSize(_storage, hnd.path.c_str(), &sz))
    {
        log::error("[sdl_storage] cannot stat file: %s", hnd.path.c_str());
        return false;
    }

    dst.resize(static_cast<size_t>(sz) + (zero_terminate ? 1 : 0));
    if(!SDL_ReadStorageFile(_storage, hnd.path.c_str(), dst.data(), sz))
    {
        log::error("[sdl_storage] cannot read file: %s", hnd.path.c_str());
        dst.clear();
        return false;
    }

    if(zero_terminate)
    {
        dst[static_cast<size_t>(sz)] = '\0';
    }

    return true;
}

} // namespace nb::res_storage