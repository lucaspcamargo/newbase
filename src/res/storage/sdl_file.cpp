#include <newbase/res/storage/sdl_file.hpp>
#include <newbase/utility/strings.hpp>
#include <newbase/nb_config.h>
#include <newbase/log.hpp>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

namespace nb::res_storage {

sdl_file::sdl_file(ryml::ConstNodeRef cfg, std::string base_path)
    : _base_path(std::move(base_path)), _index_found( false )
{
    _search_index();
}

sdl_file::~sdl_file()
{
}

bool sdl_file::writable() const
{
    // not for now
    return false;
}

bool sdl_file::scannable() const
{
    return true;
}

bool sdl_file::has_index() const
{
    return _index_found;
}

std::vector<asset_handle> sdl_file::get_handles(bool try_scan, bool use_index)
{
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
        char ** vfiles = SDL_GlobDirectory(_base_path.c_str(), nullptr, 0, nullptr);
        if(vfiles)
        {
            for(char **curr = vfiles; *curr; curr++)
            {
                SDL_PathInfo info;
                if(SDL_GetPathInfo((_base_path + "/" + *curr).c_str(), &info))
                {
                    asset_handle handle;
                    std::string path {*curr};
                    bool absolute = path[0] == '/';
                    auto hash = entt::hashed_string(absolute? path.c_str()+1 : path.c_str());
                    handle.id = hash.value();
                    handle.name = ::nb::util::str_split(path, "/").back();
                    handle.path = path;
                    handle.size = static_cast<size_t>(info.size);
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

bool sdl_file::_search_index()
{
    bool ret = false;
    
    std::string idx_path {"index.yaml"};
    std::size_t idx_len;
    std::vector<char> buf;

    void *data = SDL_LoadFile((_base_path + "/index.yaml").c_str(), &idx_len);
    if(data)
    {
        buf.resize(static_cast<size_t>(idx_len));
        memcpy(buf.data(), data, static_cast<size_t>(idx_len));
        
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
                
        SDL_free(data);
        ret = true;
    }
    else
    {
        log::error("[sdl_file] cannot open index file: %s", (_base_path + "/index.yaml").c_str());
    }

    return ret;
}

bool sdl_file::_index_add(std::string path, size_t sz)
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


bool sdl_file::read_all_sync(const asset_handle &hnd, std::vector<char> &dst, bool zero_terminate)
{
    std::string full_path = _base_path + "/" + hnd.path;
    std::size_t file_len;
    void *data = SDL_LoadFile(full_path.c_str(), &file_len);
    if(data)
    {
        dst.resize(static_cast<size_t>(file_len) + (zero_terminate ? 1 : 0));
        memcpy(dst.data(), data, static_cast<size_t>(file_len));
        if(zero_terminate)
        {
            dst[static_cast<size_t>(file_len)] = '\0';
        }
        SDL_free(data);
        return true;
    }
    else
    {
        log::error("[sdl_file] cannot read file: %s", full_path.c_str());
        return false;
    }
}

}
