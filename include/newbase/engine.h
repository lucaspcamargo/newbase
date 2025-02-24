#pragma once

#include <vector>
#include <memory>

// fwd
extern "C" union SDL_Event;

namespace nb {

class system;
struct engine_p;

class engine final {
public:
    engine();
    engine(engine&) = delete;
    engine(engine&&) = delete;
    ~engine();

    bool init(int argc, char **argv);
    bool step();
    bool event(SDL_Event *);

private:
    engine_p *_d;
    std::vector<std::shared_ptr<system>> _systems;
};

}
