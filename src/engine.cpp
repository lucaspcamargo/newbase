#include <newbase/engine.hpp>
#include <newbase/system.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/scene.hpp>
#include <newbase/sdl/logging_handler.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/reflection/coercions.hpp>
#include <newbase/reflection/lib_glm.hpp>
#include <newbase/res/rtti.hpp>
#include <newbase/i18n/i18n.hpp>
#include <newbase/log.hpp>
#include <newbase/services/ui_manager.hpp>
#include <newbase/services/rtti_info.hpp>
#include <newbase/services/renderer_service.hpp>
#include <newbase/ui/manager.hpp>
#ifdef NEWBASE_USE_XDG_DATA_DIRS
#include <newbase/utility/xdg.h>
#endif
#ifdef NEWBASE_WII
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <entt/entt.hpp>
#include <entt/meta/utility.hpp>
#include <entt/meta/pointer.hpp>
#include <tracy/Tracy.hpp>

#include <functional>
#include <optional>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>


using namespace nb;
using entt::operator""_hs;

using framecounter_data = nb::engine::framecounter_data;

struct nb::engine_p {
    std::vector<std::shared_ptr<system>> _systems;
    std::unordered_map<entt::id_type, std::shared_ptr<system>> _systems_meta;
    std::vector<ryml::ConstNodeRef> _sys_cfgs;

    ryml::Tree cfg;
    std::string cfile;
    SDL_InitFlags initflags;
    int log_handler_handle;
    scene default_scene;

    bool exit_requested {false};
    bool paused {false};
    std::optional<entt::id_type> pending_scene_id;

    std::vector<render_layer> render_layers;

    std::array<framecounter_data, nb::step_phase::_STEP_PHASE_COUNT+1> fc_data; // last one is for total
    size_t fc_end = 0;

    std::map<int, std::string> dbg_action_names;
    std::map<int, std::function<void(void)>> dbg_action_callbacks;
    int dbg_action_next_idx = 0;

    int    argc {0};
    char** argv {nullptr};
};

engine::engine()
{
    ZoneScoped;
    log::info("[engine] constructing (thread id: %lu)", SDL_ThreadID());

    _d = new engine_p();
    _d->initflags = 0;

    log::setup_handler();
    _d->log_handler_handle = log::register_observer(std::bind(&engine::log_handler, this, 
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    log::info("[engine] logging ready");

#ifdef NEWBASE_WII
    // On Wii, SDL_LoadFile cannot be used before SDL_Init (it goes through SDL's
    // file abstraction which touches hardware state). Use POSIX open()/read() via
    // newlib/devkitPPC to read the config from the FAT SD card directly.
    {
        const char *wii_cfg_path = "/nb/config.yaml";
        int fd = open(wii_cfg_path, O_RDONLY);
        if(fd < 0)
        {
            log::critical("[engine] could not open config file: %s", wii_cfg_path);
            exit(1);
        }
        struct stat st;
        fstat(fd, &st);
        _d->cfile.resize(st.st_size);
        read(fd, _d->cfile.data(), st.st_size);
        close(fd);
    }
#else
    std::string cfgpath = std::string(NEWBASE_DEFAULT_RES_PREFIX) + "/config.yaml";
#ifdef NEWBASE_USE_XDG_DATA_DIRS
    if(_nb_xdg_data_dir_found())
    {
        cfgpath = _nb_xdg_data_dirname_get() + std::string{"/"} + cfgpath;
    }
#endif
    void *data = SDL_LoadFile(cfgpath.c_str(), nullptr);
    if(!data)
    {
        log::critical("[engine] could not open config file: %s", cfgpath.c_str());
        exit(1);
    }
    _d->cfile = std::string{reinterpret_cast<const char *>(data)};
    SDL_free(data);
#endif

    _d->cfg = ryml::parse_in_place(_d->cfile.data());

    // could be useful but no
    //log::info("[engine] config:\n%s",
    //            ryml::emitrs_yaml<std::string>(_d->cfg).c_str());

    auto resources = _d->cfg["resources"];
    if(!rman().configure(resources))
    {
        log::critical("[engine] could not initialize resource manager!");
        exit(1);
    }

    nb::i18n::init(_d->cfg.rootref());

    auto systems = _d->cfg["systems"];
    for(ryml::ConstNodeRef n : systems.children())
    {
        ZoneNamed(scope_sys_init, true);
        ZoneTextV(scope_sys_init, n.key().str, n.key().len);

        std::string sysname;
        c4::from_chars(n.key(), &sysname);
        log::info("[engine] creating system: %s", sysname.c_str());
        std::shared_ptr<system> sys = system::build(sysname.c_str(), n.tree());
        if(!sys)
        {
            log::error("[engine] could not create system: %s", sysname.c_str());
        }
        else
        {
            _d->initflags |= sys->sdl_subsystems(n);
            _d->_systems.emplace_back(sys);
            _d->_systems_meta[sys->metatype_id()] = sys;
            _d->_sys_cfgs.push_back(n);
        }
    }

    log::info("[engine] constructed");
}

engine::~engine()
{
    log::info("[engine] destroying");

    // ensure there are not more system references from the engine
    _d->_systems.clear();
    _d->_systems_meta.clear();

    log::unregister_observer(_d->log_handler_handle);
    delete _d;

    log::info("[engine] destroyed");
}

bool engine::init(int argc, char ** argv)
{
    ZoneScoped;
    log::info("[engine] init");
    _d->argc = argc;
    _d->argv = argv;

    _register_default_services();

    // TODO collect SDL systems to init from systems
    if(!SDL_Init(_d->initflags))
    {
        log::error("[engine] SDL_Init error: (%d): %s", static_cast<int>(_d->initflags), SDL_GetError());
        return false;
    }

    auto cfg_it = _d->_sys_cfgs.begin();
    for(auto s: _d->_systems)
    {
        if(!s->init(*cfg_it++))
            return false;
    }

    // load initial entity tree
    auto root_ent = _d->default_scene.build_etree("res/root.et.yaml"_hs);
    log::info("[engine] root tree: %x", root_ent);

    log::info("[engine] initialized");
    return true;
}

int    engine::argc() const { return _d->argc; }
char** engine::argv() const { return _d->argv; }

bool engine::teardown()
{
    // destroy all system shared_ptrs
    _d->_systems.clear();
    _d->_systems_meta.clear();
    nb::i18n::shutdown();
    ::nb::rman().clear();
    return true;
}

void engine::request_scene_change(entt::id_type etree_id)
{
    _d->pending_scene_id = etree_id;
}

bool engine::step()
{
    ZoneScoped;
    log::verb("[engine] step");

    if(_d->exit_requested)
    {
        log::info("[engine] exiting");
        return false;
    }

    if(_d->pending_scene_id.has_value())
    {
        ZoneNamed(scope_scene_change, true);
        entt::id_type next = *_d->pending_scene_id;
        _d->pending_scene_id.reset();
        log::info("[engine] scene change: %x", next);
        for(auto &s : _d->_systems)
            s->on_scene_change();
        _d->default_scene.clear();
        _d->default_scene.build_etree(next);
    }

    bool ret = true;

    for(int i = 0; i < nb::step_phase::_STEP_PHASE_COUNT; i++)
    {
        ZoneNamed(scope_update_phase, true);
        ZoneTextVF(scope_update_phase, "phase %d", i);

        if(ret)
        {
            auto phase = static_cast<nb::step_phase>(i);
            if(_d->paused && (phase == step_phase::PHYSICS_UPDATE || phase == step_phase::GENERAL_UPDATE))
                continue;
            uint64_t start_time = SDL_GetTicksNS();

            for(auto s: _d->_systems)
            {
                // TODO check if system uses this phase (use some sort of mask?)
                if(!s->step(phase))
                {
                    ret = false;
                    break;
                }
            }
            _d->fc_data[i].fc_phase_start[_d->fc_end] = start_time;
            _d->fc_data[i].fc_phase_end[_d->fc_end] = SDL_GetTicksNS();
        }
        else
            break;
    }

    // update totals and advance end index
    _d->fc_data[step_phase::_STEP_PHASE_COUNT].fc_phase_start[_d->fc_end] = _d->fc_data[0].fc_phase_start[_d->fc_end];
    _d->fc_data[step_phase::_STEP_PHASE_COUNT].fc_phase_end[_d->fc_end] = _d->fc_data[step_phase::_STEP_PHASE_COUNT-1].fc_phase_end[_d->fc_end];
    _d->fc_end++;
    _d->fc_end %= NB_FRAMECOUNTER_SAMPLES;

    return ret;
}

bool engine::event(SDL_Event *evt)
{
    log::verb("[engine] event");
    if(evt->type == SDL_EVENT_KEY_DOWN)
    {
#ifndef __EMSCRIPTEN__
        if(evt->key.key == SDLK_ESCAPE)
            return false;
        else
#endif
        if(evt->key.key >= SDLK_F1 && evt->key.key <= SDLK_F12)
        {
            int idx = evt->key.key - SDLK_F1 + 1;

            auto it = _d->dbg_action_callbacks.find(idx);
            if(it != _d->dbg_action_callbacks.end())
            {
                log::info("[engine] debug action triggered: (%d) '%s'", it->first, _d->dbg_action_names[idx].c_str());
                it->second();
            }
        }
        // check for grave scancode, and do debug action 0
        else if(evt->key.scancode == SDL_SCANCODE_GRAVE)
        {
            auto it = _d->dbg_action_callbacks.find(0);
            if(it != _d->dbg_action_callbacks.end())
            {
                log::info("[engine] debug action triggered: (%d) '%s'", it->first, _d->dbg_action_names[0].c_str());
                it->second();
            }
        }
    }
    
    if(evt->type == SDL_EVENT_QUIT)
        return false;

    for(auto s: _d->_systems)
    {
        // TODO check if system takes event (use some sort of mask?)
        if(!s->event(evt))
            return false;
    }

    return true;
}

::nb::scene& engine::default_scene()
{
    return _d->default_scene;
}

::nb::scene* engine::find_scene(entt::id_type /*scene_id*/)
{
    // TODO: support multiple named scenes; for now everything maps to default_scene
    return &_d->default_scene;
}

void engine::add_render_layer(const render_layer &layer)
{
    log::info("[engine] add_render_layer: order=%d camera=%u vp=%u mask=0x%x total=%zu",
        layer.order, entt::to_integral(layer.camera), layer.viewport, layer.layer_mask,
        _d->render_layers.size() + 1);
    _d->render_layers.push_back(layer);
    std::stable_sort(_d->render_layers.begin(), _d->render_layers.end(),
        [](const render_layer &a, const render_layer &b){ return a.order < b.order; });
}

void engine::remove_render_layer(int order)
{
    auto &v = _d->render_layers;
    v.erase(std::remove_if(v.begin(), v.end(),
        [order](const render_layer &l){ return l.order == order; }), v.end());
}

void engine::clear_render_layers()
{
    _d->render_layers.clear();
}

const std::vector<render_layer>& engine::render_layers() const
{
    return _d->render_layers;
}


void engine::request_exit()
{
    log::info("[engine] exit requested");
    _d->exit_requested = true;
}

void engine::set_paused(bool paused)
{
    _d->paused = paused;
}

bool engine::is_paused() const
{
    return _d->paused;
}

void engine::_register_default_services()
{
    // we use a simple ui manager by default
    // this can be overriden by the editor system if enabled
    entt::locator<ui_manager*>::emplace(new ui_manager_simple());
}


std::shared_ptr<::nb::system> engine::system_from_id(entt::id_type meta_id)
{
    auto it = _d->_systems_meta.find(meta_id);
    return it != _d->_systems_meta.end()? it->second : nullptr;
}

void engine::register_system(std::shared_ptr<::nb::system> sys)
{
    static const ryml::Tree empty_tree = ryml::parse_in_arena("{}");
    if (sys->init(empty_tree.rootref()))
    {
        _d->_systems.push_back(sys);
        _d->_systems_meta[sys->metatype_id()] = sys;
    }
}

int engine::debug_action_register(std::string name, std::function<void(void)> callback,  int idx)
{
    if(idx == -1)
    {
        idx = _d->dbg_action_next_idx++;
    }

    _d->dbg_action_names[idx] = name;
    _d->dbg_action_callbacks[idx] = callback;
    return idx;
}

bool engine::debug_action_trigger(int idx)
{
    auto it = _d->dbg_action_callbacks.find(idx);
    if(it != _d->dbg_action_callbacks.end())
    {
        log::info("[engine] debug action triggered: (%d) '%s'", it->first, _d->dbg_action_names[idx].c_str());
        it->second();
        return true;
    }
    return false;
}

bool engine::debug_action_unregister(int index)
{
    if(_d->dbg_action_names.find(index) != _d->dbg_action_names.end())
    {
        _d->dbg_action_names.erase(index);
        _d->dbg_action_callbacks.erase(index);
        return true;
    }
    return false;
}

const std::map<int, std::string>& engine::debug_action_names()
{
    return _d->dbg_action_names;
}

void engine::log_handler(int category, int prio, const char *msg)
{
    static bool checked_color = false;
    static bool use_color = false;
    if(!checked_color)
    {
        // maybe move check to ::nb::log?
        const char * term = SDL_getenv("TERM");
        if(term && strstr(term, "xterm") == term)
            use_color = true;
        checked_color = true;
    }
    auto ansi = ::nb::log::priority_ansi_decor(static_cast<::nb::log::priority>(prio));
    std::cout << (ansi.first? ansi.first : "") <<"["<< ::nb::log::priority_str(static_cast<::nb::log::priority>(prio)) <<
         "] [" << ::nb::log::category_str(static_cast<::nb::log::category>(category)) << "] "<< msg << (ansi.second? ansi.second : "") << std::endl;
}


framecounter_data& engine::frametime_data(int phase)
{
    return _d->fc_data[static_cast<size_t>(phase)];
}

int engine::frametime_data_offset()
{
    return _d->fc_end;
}


engine& engine::instance()
{
    static engine e;
    return e;
}

extern "C" void _rtti_init_engine()
{
    ::nb::rtti::register_coercions();
    ::nb::rtti::_rtti_init_resources();
    ::nb::rtti::register_lib_glm();
    ::nb::rtti::_rtti_init_services();

    /* unsure non how to register singletons, asked on discord
       can possibly can be solved via some sort of proxy type that forwards to the singleton instance?
    */

    entt::meta_factory<nb::render_layer>{}
    .type("render_layer"_hs)
    .custom<rtti::type_info>(rtti::type_info{.identifier="render_layer", .type_class=rtti::TYPE_CLASS_NONE})
    .ctor<>()
    .data<&nb::render_layer::scene_id>("scene_id"_hs)
        .custom<rtti::data_info>(rtti::data_info{"scene_id"})
    .data<&nb::render_layer::layer_mask>("layer_mask"_hs)
        .custom<rtti::data_info>(rtti::data_info{"layer_mask"})
    .data<&nb::render_layer::camera>("camera"_hs)
        .custom<rtti::data_info>(rtti::data_info{"camera"})
    .data<&nb::render_layer::viewport>("viewport"_hs)
        .custom<rtti::data_info>(rtti::data_info{"viewport"})
    .data<&nb::render_layer::order>("order"_hs)
        .custom<rtti::data_info>(rtti::data_info{"order"});

    entt::meta_factory<nb::engine>{}
    .type("engine"_hs)
    .custom<rtti::type_info>(rtti::type_info{.identifier="engine", .type_class=rtti::TYPE_CLASS_SINGLETON})
    .func<&nb::engine::request_exit>("request_exit"_hs)
        .custom<rtti::func_info>(rtti::func_info{"request_exit"})
    .func<&nb::engine::add_render_layer>("add_render_layer"_hs)
        .custom<rtti::func_info>(rtti::func_info{"add_render_layer"})
    .func<&nb::engine::remove_render_layer>("remove_render_layer"_hs)
        .custom<rtti::func_info>(rtti::func_info{"remove_render_layer"})
    .func<&nb::engine::clear_render_layers>("clear_render_layers"_hs)
        .custom<rtti::func_info>(rtti::func_info{"clear_render_layers"});
}
