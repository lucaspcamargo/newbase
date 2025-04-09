#pragma once

#include <newbase/system.h>

namespace nb {

// this system takes care of "update callbacks" and other timing stuff
class clock : public system
{
public:
    clock();
    ~clock();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override {return 0;}
    entt::id_type metatype_id() override { return entt::hashed_string{"clock"}.value(); }
    bool can_bind() override { return true; }
    void bind(void *state) override;

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;

    int update_add(std::function<void(float)> func)
    {
        m_update.emplace(++m_update_counter, func);
        return m_update_counter;
    }

private:
    std::map<int, std::function<void(float)>> m_update;
    int m_update_counter {0};
};

}