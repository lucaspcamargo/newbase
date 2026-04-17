#pragma once

#include <newbase/system.hpp>
#include <newbase/utility/meta_callback.hpp>

namespace nb {

// this system takes care of "update callbacks" and other timing stuff
class clock : public system
{
public:
    clock();
    ~clock();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override {return 0;}
    entt::id_type metatype_id() override { return entt::hashed_string{"clock"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;

    int update_add(meta_callback cb)
    {
        m_update.emplace(++m_update_counter, std::move(cb));
        return m_update_counter;
    }

    void on_scene_change() override
    {
        m_update.clear();
        m_update_counter = 0;
    }

private:
    std::map<int, meta_callback> m_update;
    int m_update_counter {0};
};

}