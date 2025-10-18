#include <newbase/utility/xdg.hpp>
#include <newbase/utility/strings.hpp>
#include <newbase/log.hpp>
#include <string>
#include <SDL3/SDL.h>

// a place to store xdg data

std::string _nb_xdg_data_dir {};
bool _nb_xdg_data_dir_found_flag {false};

void _nb_xdg_data_dirname_search(const char* dirname)
{
    using namespace nb;

    _nb_xdg_data_dir = "";
    _nb_xdg_data_dir_found_flag = false;

    if(getenv("XDG_DATA_DIRS"))
    {
        std::string xdg_data_dirs {getenv("XDG_DATA_DIRS")};
        log::info("[rmanager] XDG data paths: %s", xdg_data_dirs.c_str());
        auto xdg_data_paths = util::str_split(xdg_data_dirs, ":");
        for (const std::string &xdg_data_path: xdg_data_paths)
        {
            if(xdg_data_path.size() == 0 || xdg_data_path[0] != '/')
                continue; // must be absolute path 
            log::info("[xdg] data: probing '%s'", xdg_data_path.c_str());
            std::string candidate = xdg_data_path + "/" + dirname;
            SDL_PathInfo pinfo;
            if(SDL_GetPathInfo(candidate.c_str(), &pinfo) && pinfo.type == SDL_PATHTYPE_DIRECTORY)
            {
                log::info("[xdg] data: found '%s'", candidate.c_str());
                _nb_xdg_data_dir = candidate;
                _nb_xdg_data_dir_found_flag = true;
                return;   
            }
        }
    }
    else
        log::warn("[xdg] usage of XDG data dirs is enabled, but XDG_DATA_DIRS is not set!");
}

bool _nb_xdg_data_dir_found()
{
    return _nb_xdg_data_dir_found_flag;
}

const char* _nb_xdg_data_dirname_get()
{
    return _nb_xdg_data_dir.c_str();
}