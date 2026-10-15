#pragma once

#include <newbase/res/texture.hpp>
#include <newbase/utility/glm.hpp>
#include <memory>
#include <utility>

namespace nb
{

class rmaterial;
class rmesh;

/// Environment input and generated resources used for scene lighting.
struct cenvironment
{
    glm::vec3 ambient {0.f};

    std::shared_ptr<rtexture> equirectangular;
    std::shared_ptr<rtexture> environment_cubemap;
    std::shared_ptr<rtexture> irradiance_cubemap;
    std::shared_ptr<rtexture> prefiltered_cubemap;
    std::shared_ptr<rmesh> skybox_mesh;
    std::shared_ptr<rmaterial> skybox_material;

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
             m_prepared_source.lock() != equirectangular);
    }

    /// Called after preparation passes have been emitted for the current source.
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
