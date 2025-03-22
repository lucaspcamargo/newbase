#pragma once

#include <newbase/mixins.h>
#include <entt/core/ident.hpp>
#include <vector>
#include <memory>

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

    // default engine log handler
    void log_handler(int category, int prio, const char *msg);
    static engine& instance();

private:
    engine();
    ~engine();

    engine_p *_d;
    std::vector<std::shared_ptr<system>> _systems;
};

}
