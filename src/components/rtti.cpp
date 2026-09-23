#include <newbase/components/rtti.hpp>

#include <newbase/components/spatial.hpp>
#include <newbase/components/structure.hpp>
#include <newbase/components/sprite.hpp>
#include <newbase/components/mesh2d.hpp>
#include <newbase/components/particle_emitter.hpp>
#include <newbase/components/camera.hpp>
#include <newbase/components/layers.hpp>

namespace nb::rtti
{
    void _rtti_init_components()
    {
        cspatial::_ensure_rtti();
        cstructure::_ensure_rtti();
        csprite::_ensure_rtti();
        cmesh2d::_ensure_rtti();
        cparticle_emitter::_ensure_rtti();
        ccamera::_ensure_rtti();
        clayers::_ensure_rtti();
    }
}
