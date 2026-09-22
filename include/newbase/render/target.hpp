#pragma once

#include <newbase/render/types.hpp>
#include <newbase/services/renderer_service.hpp>

namespace nb::render {

/// RAII wrapper for render target handles
/// render target is managed by the renderer via the provided service
class target_ref
{
public:
    target_ref() = default;

    // Constructs and immediately allocates the GPU render target via the service interface
    target_ref(renderer_service& service, const target_desc& desc)
    : m_service(&service)
    {
        m_id = m_service->target_create(desc);
    }

    ~target_ref() {
        reset();
    }

    // Move-only semantics
    target_ref(const target_ref&) = delete;
    target_ref& operator=(const target_ref&) = delete;

    target_ref(target_ref&& other) noexcept
    : m_service(other.m_service), m_id(other.m_id)
    {
        other.m_service = nullptr;
        other.m_id = TARGET_INVALID;
    }

    target_ref& operator=(target_ref&& other) noexcept {
        if (this != &other) {
            reset();
            m_service = other.m_service;
            m_id = other.m_id;
            other.m_service = nullptr;
            other.m_id = TARGET_INVALID;
        }
        return *this;
    }

    void reset() {
        if (m_id != TARGET_INVALID && m_service) {
            m_service->target_destroy(m_id);
            m_id = TARGET_INVALID;
            m_service = nullptr;
        }
    }

    // Raw Handle
    target_id_t id() const { return m_id; }
    explicit operator bool() const { return m_id != TARGET_INVALID; }

    // Convenience property getters that forward to m_service
    std::shared_ptr<rtexture> color_texture() const {
        return (m_id != TARGET_INVALID && m_service) ? m_service->target_get_color_texture(m_id) : 0;
    }

    std::shared_ptr<rtexture> depth_texture() const {
        return (m_id != TARGET_INVALID && m_service) ? m_service->target_get_depth_texture(m_id) : 0;
    }

    glm::ivec2 size() const {
        return (m_id != TARGET_INVALID && m_service) ? m_service->target_get_size(m_id) : glm::ivec2{0, 0};
    }

    int width() const { return size().x; }
    int height() const { return size().y; }

    bool has_depth() const {
        return (m_id != TARGET_INVALID && m_service) ? m_service->target_has_depth(m_id) : false;
    }

private:
    renderer_service* m_service { nullptr };
    target_id_t  m_id { TARGET_INVALID };
};

}
