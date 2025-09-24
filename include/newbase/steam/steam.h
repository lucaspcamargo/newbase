#pragma once

#include "newbase/system.h"

namespace nb {

struct steam_p;

class steam : public system {
public:
    steam();
    ~steam() override;

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"steam"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase phase) override;
    bool event(SDL_Event *ev) override;

    static std::shared_ptr<system> build(const std::string &id, const void *cfgnode);

private:
    std::unique_ptr<steam_p> _d;
};

} // namespace nb
