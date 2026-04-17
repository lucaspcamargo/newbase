#pragma once

#include <newbase/mixins.hpp>
#include <newbase/nb_config.h>
#include <newbase/layer.hpp>
#include <entt/core/ident.hpp>
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <map>
#include <optional>

// fwd
extern "C" union SDL_Event;

namespace nb {

class system;
class scene;
struct engine_p;

class engine final : public nocopy {
public:
    engine(const engine &) = delete;
    engine(engine &&) = delete;
    engine& operator=(const engine &) = delete;
    engine& operator=(engine &&) = delete;


    // entry points
    bool init(int argc, char **argv);
    bool step();
    bool event(SDL_Event *);
    bool teardown();

    // scenes
    ::nb::scene& default_scene();
    // Returns a scene by id, or nullptr if not found.
    // Currently only the default scene is supported (any id maps to it).
    ::nb::scene* find_scene(entt::id_type scene_id);
    // Queue a scene change: at the start of the next step, all systems are notified,
    // the current scene is cleared, and the given etree is built into the fresh scene.
    void request_scene_change(entt::id_type etree_id);

    // render layers
    void add_render_layer(const render_layer &layer);
    void remove_render_layer(int order);
    void clear_render_layers();
    const std::vector<render_layer>& render_layers() const;

    void request_exit();
    void set_paused(bool paused);
    bool is_paused() const;

    std::shared_ptr<::nb::system> system_from_id(entt::id_type meta_id);

    // Register a system that was instantiated directly (not from config).
    // SDL is already initialised at this point. Calls sys->init() with an empty config.
    void register_system(std::shared_ptr<::nb::system> sys);

    // global debug actions
    // allows registering of simple callbacks that are invoked using function/number keys
    int debug_action_register(std::string name, std::function<void(void)> callback, int idx = -1);
    bool debug_action_unregister(int index);
    bool debug_action_trigger(int idx);
    const std::map<int, std::string> & debug_action_names();

    // default engine log handler
    void log_handler(int category, int prio, const char *msg);
    static engine& instance();

    struct framecounter_data
    {
        std::array<uint64_t, NB_FRAMECOUNTER_SAMPLES> fc_phase_start {};
        std::array<uint64_t, NB_FRAMECOUNTER_SAMPLES> fc_phase_end {};
    };

    framecounter_data& frametime_data(int phase);
    int frametime_data_offset();

private:
    engine();

    void _register_default_services();

public:  // HACK -- entt::meta does not like private destructors
    ~engine();
private:  // HACK -- entt::meta does not like private destructors
    engine_p *_d;
};

}
