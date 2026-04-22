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

    int update_add_monotonic(meta_callback cb)
    {
        m_update_monotonic.emplace(++m_update_counter, std::move(cb));
        return m_update_counter;
    }

    void update_remove(int handle)
    {
        m_update.erase(handle);
        m_update_monotonic.erase(handle);
    }

    void on_scene_change() override
    {
        m_update.clear();
        m_update_monotonic.clear();
        m_update_counter = 0;
    }

    void  set_time_scale(float s) { m_time_scale = s; }
    float get_time_scale() const  { return m_time_scale; }
    float get_real_dt()    const  { return m_real_dt; }
    float get_dt()         const  { return m_real_dt * m_time_scale; }

private:
    std::map<int, meta_callback> m_update;
    std::map<int, meta_callback> m_update_monotonic;
    int      m_update_counter {0};
    float    m_time_scale     {1.0f};
    float    m_real_dt        {0.0f};
    uint64_t m_last_ns        {0};
};

}