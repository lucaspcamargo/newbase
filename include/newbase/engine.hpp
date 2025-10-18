#pragma once

#include <newbase/mixins.hpp>
#include <entt/core/ident.hpp>
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <map>

// fwd
extern "C" union SDL_Event;

namespace nb {

class system;
class scene;
struct engine_p;

class engine final : public nocopy {
public:

    // entry points
    bool init(int argc, char **argv);
    bool step();
    bool event(SDL_Event *);
    bool teardown();

    // scenes
    ::nb::scene& default_scene();
    // think about these later:
    //bool has_scene(entt::id_type id);
    //::nb::scene& scene_ref(entt::id_type id);
    //bool destroy_scene(entt::id_type id);

    void request_exit();

    std::shared_ptr<::nb::system> system_from_id(entt::id_type meta_id);

    // global debug actions
    // allows registering of simple callbacks that are invoked using function/number keys
    int debug_action_register(std::string name, std::function<void(void)> callback, int idx = -1);
    bool debug_action_unregister(int index);
    const std::map<int, std::string> & debug_action_names();

    // default engine log handler
    void log_handler(int category, int prio, const char *msg);
    static engine& instance();

private:
    engine();
    ~engine();

    engine_p *_d;
};

}
