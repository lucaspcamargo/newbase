#pragma once

#include <newbase/res/texture.hpp>
#include <memory>
#include <utility>

namespace nb
{

/// Environment input and generated resources used for scene lighting.
struct cenvironment
{
    std::shared_ptr<rtexture> equirectangular;
    std::shared_ptr<rtexture> environment_cubemap;
    std::shared_ptr<rtexture> irradiance_cubemap;
    std::shared_ptr<rtexture> prefiltered_cubemap;

    /// Assigning a different source schedules environment preparation.
    void set_equirectangular(std::shared_ptr<rtexture> source)
    {
        if(equirectangular == source)
            return;
        equirectangular = std::move(source);
        m_preparation_pending = true;
    }

    bool preparation_pending() const
    {
        return equirectangular &&
            (m_preparation_pending ||
             m_prepared_source.lock() != equirectangular ||
             render::has_flag(equirectangular->flags, render::texture_flags::NEEDS_UPLOAD));
    }

    /// Called by the renderer only after all derived resources are ready.
    void mark_prepared()
    {
        m_preparation_pending = false;
        m_prepared_source = equirectangular;
    }

    void request_preparation()
    {
        m_preparation_pending = true;
    }

    static void _ensure_rtti();

private:
    bool m_preparation_pending {true};
    std::weak_ptr<rtexture> m_prepared_source;
};

}
