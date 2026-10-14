#pragma once

#include <newbase/render/pass.hpp>
#include <memory>

namespace nb::render
{

/// Builds the cubemap conversion, irradiance, and specular prefilter passes
/// needed by pending scene environments.
class ibl_intg_pass_buider final : public pass_builder
{
public:
    build_result build(pass_build_context &context,
                       const pass_build_request &request) override;

private:
    std::shared_ptr<rmesh> m_fullscreen_quad;
};

}
