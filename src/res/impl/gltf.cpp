#include "entt/core/fwd.hpp"
#include "newbase/reflection/resources.hpp"
#include <newbase/res/gltf.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>

#include <tiny_gltf_v3.h>
#include <algorithm>
#include <filesystem>
#include <string>


// TODO implement fs callback from rman vfs
//      when trying to load textures, don't
//      instead, return a descriptor for rtexture to know where to load
//      the real texture from (add this descriptor to res/texture.hpp as a
//      standard mechanism)

// NOTE with tg3 we cannot just parse the structure without loadingthe buffers (I think?)
//      so most all of our loading is done on preload


// callbacks

static int32_t _nb_gltf_file_exists(const char *path, uint32_t path_len,
                                      void *user_data);
static int32_t _nb_gltf_read_file(uint8_t **out_data, uint64_t *out_size,
                                    const char *path, uint32_t path_len,
                                    void *user_data);
static void _nb_gltf_free_file(uint8_t *data, uint64_t size,
                                 void *user_data);
static int32_t _nb_gltf_write_file(const char *path, uint32_t path_len,
                                     const uint8_t *data, uint64_t size,
                                     void *user_data);
static int32_t _nb_gltf_resolve_path(char *out_path, uint32_t out_cap,
                                       uint32_t *out_len,
                                       const char *path, uint32_t path_len,
                                       void *user_data);
static int32_t _nb_gltf_get_file_size(uint64_t *out_size,
                                        const char *path, uint32_t path_len,
                                        void *user_data);
static int32_t _nb_gltf_load_image(tg3_image_result *result,
                                     const tg3_image_request *request,
                                     void *user_data);
static void _nb_gltf_free_image(uint8_t *pixels, void *user_data);
static int32_t _nb_gltf_uri_encode(char *out, uint32_t out_cap,
                                     uint32_t *out_len,
                                     const char *uri, uint32_t uri_len,
                                     const char *obj_type,
                                     void *user_data);
static int32_t _nb_gltf_uri_decode(char *out, uint32_t out_cap,
                                     uint32_t *out_len,
                                     const char *uri, uint32_t uri_len,
                                     void *user_data);


using namespace nb;


rgltf::rgltf(entt::id_type id) :
    resource(id, entt::hashed_string{"rgltf"}.value())
{

}

rgltf::~rgltf()
{
    if(m_model)
        tg3_model_free(m_model.get());
    m_model.reset();
}

bool rgltf::do_preload()
{
    log::info("[rgltf] loading: 0x%08x", id());

    if (!rman().read_all_sync(id(), m_data))
    {
        log::error("[rgltf] do_preload: data loading failed: %x", id());
        return false;
    }

    std::string base_dir {"."};
    const auto &rman_handles = rman().handles();
    const auto hnd_it = rman_handles.find(id());
    if(hnd_it == rman_handles.end())
    {
        log::warn("[rgltf] model not in vfs, base dir set to '.'");
    }

    const auto &vfs_path = hnd_it->second.path;
    auto path = std::filesystem::path{vfs_path};
    if(path.has_parent_path())
        base_dir = path.parent_path();

    log::info("[rgltf] base dir for 0x%08x: '%s'", id(), base_dir.c_str());

    tg3_parse_options opts;
    tg3_error_stack errors;
    m_model = std::make_unique<tg3_model>();

    tg3_parse_options_init(&opts);
    tg3_error_stack_init(&errors);

    opts.parse_float32 = 1;
    opts.images_as_is = 1;
    opts.fs.file_exists = _nb_gltf_file_exists;
    opts.fs.read_file = _nb_gltf_read_file;
    opts.fs.resolve_path = _nb_gltf_resolve_path;
    opts.fs.free_file = _nb_gltf_free_file;
    opts.fs.write_file = nullptr;
    opts.fs.user_data = this;
    opts.image.load_image = _nb_gltf_load_image;
    opts.image.free_image = _nb_gltf_free_image;
    opts.image.user_data = this;
    opts.uri.decode = _nb_gltf_uri_decode;
    opts.uri.encode = _nb_gltf_uri_encode;
    opts.uri.user_data = this;

    tg3_error_code err = tg3_parse_auto(m_model.get(), &errors,
                                        (uint8_t*)m_data.data(), m_data.size(),
                                        base_dir.c_str(), base_dir.size(), &opts);
    if (err != TG3_OK) {
        for (uint32_t i = 0; i < errors.count; i++) {
            log::error("[rgltf] do_preload: load error %d: [%d] %s\n", 1, (int)errors.entries[i].severity,
                    errors.entries[i].message ? errors.entries[i].message : "(null)");
        }
    }
    else
        log::info("[rgltf] model load ok: 0x%08x: %d meshes, %d materials, %d textures",
                  id(), m_model->meshes_count, m_model->materials_count, m_model->textures_count);

    // now go over internal resources, and register them with the resource manager
    for(int i = 0; i < m_model->meshes_count; i++)
    {
        auto sub_path = (std::string{path} + "?msh=" + std::to_string(i));
        m_mesh_res_ids.push_back(rman().note_subresource(sub_path));
    }
    for(int i = 0; i < m_model->materials_count; i++)
    {
        auto sub_path = (std::string{path} + "?mat=" + std::to_string(i));
        m_mat_res_ids.push_back(rman().note_subresource(sub_path));
    }
    for(int i = 0; i < m_model->images_count; i++) // NOTE nb textures are gltf images
    {
        auto sub_path = (std::string{path} + "?tex=" + std::to_string(i));
        m_tex_res_ids.push_back(rman().note_subresource(sub_path));
    }
    for(int i = 0; i < m_model->scenes_count; i++)
    {
        auto sub_path = (std::string{path} + "?scn=" + std::to_string(i));
        m_scn_res_ids.push_back(rman().note_subresource(sub_path));
    }

    // all subresources registered

    tg3_error_stack_free(&errors);

    return err == TG3_OK;
}


bool rgltf::do_load()
{
    return true;
}


bool rgltf::get_subresource_data(entt::id_type sub, std::vector<uint8_t> &out_vec)
{
    log::info("[rgltf] get_subresource_data: 0x%08x -> 0x%08x", id(), sub);

    if(!m_model)
    {
        log::error("[rgltf] get_subresource_data: not yet preloaded! 0x%08x -> 0x%08x", id(), sub);
        return false;
    }

    if(auto t_it = std::find(m_tex_res_ids.begin(), m_tex_res_ids.end(), sub); t_it != m_tex_res_ids.end())
    {
        auto idx = t_it - m_tex_res_ids.begin();
        auto bv_idx = m_model->images[idx].buffer_view;
        std::string mime {m_model->images[idx].mime_type.data, m_model->images[idx].mime_type.len};
        if(bv_idx >= 0)
        {
            auto &bv = m_model->buffer_views[bv_idx];
            auto buf_idx = bv.buffer;
            if(buf_idx >= 0)
            {
                auto &buf = m_model->buffers[buf_idx];
                out_vec.resize(bv.byte_length);
                memcpy(out_vec.data(), buf.data.data + bv.byte_offset, bv.byte_length);
                return true;
            }
        }
    }
    return false;
}


int32_t _nb_gltf_file_exists(const char *path, uint32_t path_len,
                             void *user_data)
{

    nb::log::error("[rgltf] UNIMPLEMENTED: _nb_gltf_file_exists");
    return 1;
}
int32_t _nb_gltf_read_file(uint8_t **out_data, uint64_t *out_size,
                            const char *path, uint32_t path_len,
                            void *user_data)
{

    nb::log::error("[rgltf] UNIMPLEMENTED: _nb_gltf_read_file");
    return 1;
}
void _nb_gltf_free_file(uint8_t *data, uint64_t size, void *user_data)
{

    nb::log::error("[rgltf] UNIMPLEMENTED: _nb_gltf_free_file");
}
int32_t _nb_gltf_write_file(const char *path, uint32_t path_len,
                                   const uint8_t *data, uint64_t size,
                            void *user_data)
{
    nb::log::error("[rgltf] UNIMPLEMENTED: _nb_gltf_resolve_path");
    return 1;
}
int32_t _nb_gltf_resolve_path(char *out_path, uint32_t out_cap,
                                     uint32_t *out_len,
                                     const char *path, uint32_t path_len,
                                     void *user_data)
{

    nb::log::error("[rgltf] UNIMPLEMENTED: _nb_gltf_resolve_path");
    return 1;
}

int32_t _nb_gltf_get_file_size(uint64_t *out_size,
                                      const char *path, uint32_t path_len,
                                      void *user_data)
{
    nb::log::error("[rgltf] UNIMPLEMENTED: _nb_gltf_get_file_size");
    return 1;
}

int32_t _nb_gltf_load_image(tg3_image_result *result,
                                   const tg3_image_request *request,
                            void *user_data)
{
    nb::log::error("[rgltf] UNIMPLEMENTED: _nb_gltf_load_image. (image decoding handled by rtexture)");
    return 1;
}

void _nb_gltf_free_image(uint8_t *pixels, void *user_data)
{
    nb::log::error("[rgltf] UNIMPLEMENTED: _nb_gltf_free_image. (image decoding handled by rtexture)");
}

int32_t _nb_gltf_uri_encode(char *out, uint32_t out_cap,
                                   uint32_t *out_len,
                                   const char *uri, uint32_t uri_len,
                                   const char *obj_type,
                                   void *user_data)
{
    nb::log::error("[rgltf] UNIMPLEMENTED: _nb_gltf_uri_encode: %.*s", uri_len, uri);
    return 1;
}
int32_t _nb_gltf_uri_decode(char *out, uint32_t out_cap,
                                   uint32_t *out_len,
                                   const char *uri, uint32_t uri_len,
                                   void *user_data)
{
    nb::log::error("[rgltf] UNIMPLEMENTED: _nb_gltf_uri_decode: %.*s", uri_len, uri);
    return 1;
}



// RTTI

void rgltf::_init_rtti()
{
    entt::meta_factory<rgltf>{}
    .type(entt::hashed_string{"rgltf"}.value())
    .custom<rtti::type_info>(rtti::type_info{
        .identifier = "gltf",
        .type_class = nb::rtti::TYPE_CLASS_RESOURCE,
        .data = {.resource = {.editor_icon = nullptr, .extensions = "",
            .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource>
            {
                return std::make_shared<rgltf>(id);
            }
        }}
    });

    rtti::res_ptr_registration<rgltf>("rgltf");
}
