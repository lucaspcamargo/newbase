#include "entt/core/fwd.hpp"
#include "entt/entity/entity.hpp"
#include <newbase/res/mesh.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/gltf.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/reflection/resources.hpp>
#include <newbase/render/vertex.hpp>
#include <tiny_gltf_v3.h>
#include <mikktspace.h>
#include <memory>

using namespace ::nb;
using namespace ::nb::render;


// helpers

struct accessor_info
{
    const uint8_t* data{nullptr};
    size_t stride{0};
    size_t count{0};
    int component_type{0};
};

// used to map the attributes on our chosen vertex format to mesh data (via accessor)
// a default value can be used instead of the mesh does not contain matching data
struct attribute_binding
{
    uint32_t offset;          // Vertex offset from descriptor
    uint32_t size;            // Attribute byte size from descriptor
    accessor_info info;       // glTF buffer accessor source
    std::array<uint8_t, 16> default_value; // Fallback if primitive lacks attribute
};


struct MikkMeshContext
{
    uint8_t  *buf_vtx;
    uint32_t *buf_ind;
    uint32_t vert_count;
    uint32_t ind_count;
    accessor_info pos_access;
    accessor_info norm_access;
    accessor_info uv_access;
    accessor_info tang_access;
};


// MikkTSpace Callbacks
static int mikk_get_num_faces(const SMikkTSpaceContext* ctx);
static int mikk_get_num_verts_of_face(const SMikkTSpaceContext* ctx, const int face);
static void mikk_get_position(const SMikkTSpaceContext* ctx, float pos_out[], const int face, const int vert);
static void mikk_get_normal(const SMikkTSpaceContext* ctx, float norm_out[], const int face, const int vert);
static void mikk_get_tex_coord(const SMikkTSpaceContext* ctx, float uv_out[], const int face, const int vert);
static void mikk_set_tspace_basic(const SMikkTSpaceContext* ctx, const float tangent[], const float sign, const int face, const int vert);
static uint32_t get_vertex_index(const MikkMeshContext* mesh, int face, int vert_in_face);

// Mesh decode helpers
static size_t get_component_size(int component_type);
static size_t get_num_components(int type);
static int find_attribute_accessor(const tg3_primitive* prim, const char* name);
static accessor_info get_accessor_info(const tg3_model* model, int accessor_idx);
static vertex_type select_mesh_vertex_type(const tg3_mesh* mesh);
static void write_default_attribute_value(vertex_attribute_type semantic, void* dst, size_t size);
static const char* semantic_to_gltf_name(vertex_attribute_type semantic);



// impl

rmesh::rmesh(entt::id_type id) :
    resource(id, entt::hashed_string{"rmesh"}.value())
{

}


rmesh::~rmesh()
{
    if(on_delete)
        on_delete(*this, on_delete_uptr);
}

bool rmesh::do_preload()
{
    // all of our work is done during proper oading, for now
    return true;
}

bool rmesh::do_load()
{
    log::info("[rmesh] loading: 0x%08x", id());

    const auto &rman_handles = rman().handles();
    const auto it = rman_handles.find(id());
    if(it == rman_handles.end())
    {
        log::error("[rmesh] not in vfs");
        return false;
    }
    const auto &hnd = it->second;
    if(!hnd.parent_id)
    {
        log::error("[rmesh] no parent resource, can only load from gltf");
        return false;
    }

    auto rgltf_hs = entt::hashed_string{"rgltf"}.value();
    auto parent_ref = rman().create(rgltf_hs, hnd.parent_id);
    if(parent_ref->type_id() != rgltf_hs)
    {
        log::error("[rmesh] parent resource of wrong type!");
        return false;
    }

    rgltf &gltf = *((rgltf*)parent_ref.get());
    gltf.force_preload_sync();

    if(!gltf.has_model())
    {
        log::error("[rmesh] parent gltf is empty!");
        return false;
    }

    // with our parent gltf discovered and preloaded, we finally do our own loading
    auto ret = _load_gltf_model_mesh(gltf.get_model(), gltf.get_model()->meshes + (hnd.name[4]-'0'), gltf); // TODO LAZY HACK

    if(ret)
        log::info("[rmesh] loaded: 0x%08x: %d buffer bytes, %d submeshes, %d material slots",
              id(), (int)m_data.size(), (int)m_submeshes.size(), (int)m_mat_slots.size());

    return ret;
}


bool rmesh::_load_gltf_model_mesh( const tg3_model* model, const tg3_mesh* mesh,
                                   const rgltf& parent_gltf)
{
    m_submeshes.clear();
    m_data.clear();
    m_data_index_offset = {0};

    // vertex format selection
    m_vtx_type = select_mesh_vertex_type(mesh);
    const auto& desc_table = vertex_type_descriptor::descriptor_table();
    const vertex_type_descriptor& desc = desc_table.at(m_vtx_type);
    const uint32_t vtx_stride = desc_table.at(m_vtx_type).size;

    // count total vertices and indices
    uint32_t total_vertices = 0;
    uint32_t total_indices = 0;

    for (uint32_t i = 0; i < mesh->primitives_count; ++i) {
        const tg3_primitive* prim = &mesh->primitives[i];
        int pos_idx = find_attribute_accessor(prim, "POSITION");
        if (pos_idx < 0) continue;

        total_vertices += static_cast<uint32_t>(model->accessors[pos_idx].count);
        if (prim->indices >= 0) {
            total_indices += static_cast<uint32_t>(model->accessors[prim->indices].count);
        }
    }

    if (total_vertices == 0)
    {
        log::warn("[rmesh] empty mesh!");
        return false;
    }

    // calculate byte sizes with full alignment
    size_t vtx_bytes = total_vertices * vtx_stride;
    size_t aligned_vtx_bytes = (vtx_bytes + 15) & ~15; // 16-byte align boundary
    size_t idx_bytes = total_indices * sizeof(uint32_t);

    m_data_index_offset = static_cast<uint32_t>(aligned_vtx_bytes);
    m_data.resize(aligned_vtx_bytes + idx_bytes, 0);

    uint8_t* vtx_dst_head = m_data.data();
    uint32_t* idx_dst_head = reinterpret_cast<uint32_t*>(m_data.data() + m_data_index_offset);

    // interleaving pass
    uint32_t vertex_cursor = 0;
    uint32_t index_cursor = 0;

    for (uint32_t i = 0; i < mesh->primitives_count; ++i) {
        const tg3_primitive* prim = &mesh->primitives[i];
        int pos_idx = find_attribute_accessor(prim, "POSITION");
        if (pos_idx < 0) continue;  // ignore malformed primitives with no pos


        // pre-configure bindings for this primitive based on the descriptor layout
        std::vector<attribute_binding> bindings;
        bindings.reserve(desc.attributes.size());
        bool has_tangent_fmt = false;
        bool has_tangent_mesh = false;
        for (const auto& attr_desc : desc.attributes) {
            attribute_binding binding{};
            binding.offset = attr_desc.offset;
            binding.size   = conv::get_vertex_data_type_size(attr_desc.format);

            const char* gltf_name = semantic_to_gltf_name(attr_desc.attr);
            int acc_idx = gltf_name ? find_attribute_accessor(prim, gltf_name) : -1;

            if(attr_desc.attr == render::vertex_attribute_type::TANGENT)
                has_tangent_fmt = true;

            if (acc_idx >= 0) {
                binding.info = get_accessor_info(model, acc_idx);
                if(attr_desc.attr == render::vertex_attribute_type::TANGENT)
                    has_tangent_mesh = true;
            } else {
                // Populate default bytes if the primitive doesn't provide this attribute
                write_default_attribute_value(attr_desc.attr, binding.default_value.data(), binding.size);
            }

            bindings.push_back(binding);
        }

        accessor_info pos_info = get_accessor_info(model, pos_idx);
        uint32_t prim_vert_count = static_cast<uint32_t>(pos_info.count);

        // Interleave vertices dynamically
        for (uint32_t v = 0; v < prim_vert_count; ++v) {
            uint8_t* vtx_ptr = vtx_dst_head + ((vertex_cursor + v) * vtx_stride);

            for (const auto& binding : bindings) {
                uint8_t* attr_dst = vtx_ptr + binding.offset;

                if (binding.info.data) {
                    const uint8_t* attr_src = binding.info.data + (v * binding.info.stride);
                    std::memcpy(attr_dst, attr_src, binding.size);
                } else {
                    std::memcpy(attr_dst, binding.default_value.data(), binding.size);
                }
            }
        }

        // Extract submesh indices
        uint32_t prim_index_count = 0;
        if (prim->indices >= 0) {
            accessor_info idx_info = get_accessor_info(model, prim->indices);
            prim_index_count = static_cast<uint32_t>(idx_info.count);

            for (uint32_t idx = 0; idx < prim_index_count; ++idx) {
                const uint8_t* elem = idx_info.data + (idx * idx_info.stride);
                uint32_t index_val = 0;

                if (idx_info.component_type == 5123) {        // UNSIGNED_SHORT
                    index_val = *reinterpret_cast<const uint16_t*>(elem);
                } else if (idx_info.component_type == 5125) { // UNSIGNED_INT
                    index_val = *reinterpret_cast<const uint32_t*>(elem);
                } else if (idx_info.component_type == 5121) { // UNSIGNED_BYTE
                    index_val = *reinterpret_cast<const uint8_t*>(elem);
                }

                idx_dst_head[index_cursor + idx] = index_val;
            }
        }


        if(has_tangent_fmt && !has_tangent_mesh)
        {
            // Use mikktspace to generate tangent vectors, per-primitive
            MikkMeshContext mesh_data
            {
                vtx_dst_head + (vertex_cursor * vtx_stride),
                idx_dst_head + index_cursor,
                prim_vert_count,
                prim_index_count,
                get_accessor_info(model, find_attribute_accessor(prim,
                    semantic_to_gltf_name(render::vertex_attribute_type::POSITION))),

                get_accessor_info(model, find_attribute_accessor(prim,
                    semantic_to_gltf_name(render::vertex_attribute_type::NORMAL))),

                get_accessor_info(model, find_attribute_accessor(prim,
                    semantic_to_gltf_name(render::vertex_attribute_type::TEXCOORD))),

                // make synthetic accessor info for tangent writing
                // TODO use engine attribute descriptor for this
                // or better yet, instead of repurposing gltf accessors,
                // use engine-native vertex attribute descriptors for both
                // reading and writing off the output buffer :)
                accessor_info
                {
                    .data = vtx_dst_head + (vertex_cursor * vtx_stride) + offsetof(render::vertex3d_tang, tang),
                    .stride = vtx_stride,
                    .count = prim_vert_count,
                    .component_type = 5126,
                }
            };

            SMikkTSpaceInterface iface{};
            iface.m_getNumFaces = mikk_get_num_faces;
            iface.m_getNumVerticesOfFace = mikk_get_num_verts_of_face;
            iface.m_getPosition = mikk_get_position;
            iface.m_getNormal = mikk_get_normal;
            iface.m_getTexCoord = mikk_get_tex_coord;
            iface.m_setTSpaceBasic = mikk_set_tspace_basic;

            SMikkTSpaceContext ctx{};
            ctx.m_pInterface = &iface;
            ctx.m_pUserData = &mesh_data;

            if(genTangSpaceDefault(&ctx) == 0)
            {
                log::warn("[rmesh] tangent generation failed!");
            }
            else
            {
                log::info("[rmesh] generated tangents with MikkTSpace");
            }
        }


        // Record submesh slice
        submesh sub{};
        sub.material_idx = _allocate_material_slot(prim->material, parent_gltf);
        sub.vertex_offset = vertex_cursor;
        sub.index_offset  = index_cursor;
        sub.index_count   = prim_index_count;

        m_submeshes.push_back(sub);

        vertex_cursor += prim_vert_count;
        index_cursor  += prim_index_count;
    }

    return true;
}

int32_t rmesh::_allocate_material_slot(int32_t model_mat_index, const rgltf& parent_gltf)
{
    if(model_mat_index == -1)
    {
        // always allocate an empty material slot for primitives without materials
        if (m_empty_mat_slot == -1)
        {
            m_mat_slots.push_back(entt::id_type{entt::null_t{}});
            m_empty_mat_slot = m_mat_slots.size() - 1;
        }

        return m_empty_mat_slot;
    }
    else
    {
        // return of existing slot wth gltf material if exists
        auto it = std::find(m_mat_slots.begin(), m_mat_slots.end(), model_mat_index);
        if(it != m_mat_slots.end())
            return *it;
        else
        {
            // create new slot for gltf material otherwise
            m_mat_slots.push_back(parent_gltf.get_material_res_ids()[model_mat_index]);
            return m_mat_slots.size() - 1;
        }
    }
}




// helpers impl

size_t get_component_size(int component_type) {
    switch (component_type) {
        case 5120: /* BYTE */
        case 5121: /* UNSIGNED_BYTE */ return 1;
        case 5122: /* SHORT */
        case 5123: /* UNSIGNED_SHORT */ return 2;
        case 5125: /* UNSIGNED_INT */
        case 5126: /* FLOAT */ return 4;
        default: return 0;
    }
}

size_t get_num_components(int type) {
    switch (type) {
        case 1: return 1; // SCALAR
        case 2: return 2; // VEC2
        case 3: return 3; // VEC3
        case 4: return 4; // VEC4
        default: return 1;
    }
}

int find_attribute_accessor(const tg3_primitive* prim, const char* name)
{
    auto name_len = strlen(name);
    for (uint32_t i = 0; i < prim->attributes_count; ++i) {
        const tg3_str_int_pair& attr = prim->attributes[i];
        if (attr.key.len == name_len && std::strncmp(attr.key.data, name, name_len) == 0) {
            return attr.value;
        }
    }
    return -1;
}

accessor_info get_accessor_info(const tg3_model* model, int accessor_idx)
{
    if (accessor_idx < 0 || static_cast<uint32_t>(accessor_idx) >= model->accessors_count) {
        return {};
    }

    const tg3_accessor* acc = &model->accessors[accessor_idx];
    if (acc->buffer_view < 0 || static_cast<uint32_t>(acc->buffer_view) >= model->buffer_views_count) {
        return {};
    }

    const tg3_buffer_view* view = &model->buffer_views[acc->buffer_view];
    if (view->buffer < 0 || static_cast<uint32_t>(view->buffer) >= model->buffers_count) {
        return {};
    }

    const tg3_buffer* buf = &model->buffers[view->buffer];

    size_t comp_size = get_component_size(acc->component_type);
    size_t num_comps = get_num_components(acc->type);
    size_t element_size = comp_size * num_comps;

    // tightly packed if byte_stride is 0
    size_t stride = view->byte_stride ? view->byte_stride : element_size;
    const uint8_t* ptr = buf->data.data + view->byte_offset + acc->byte_offset;

    return { ptr, stride, static_cast<size_t>(acc->count), acc->component_type };
}

vertex_type select_mesh_vertex_type(const tg3_mesh* mesh)
{
    bool has_tangent = false;
    bool has_normal = false;

    for (uint32_t i = 0; i < mesh->primitives_count; ++i) {
        const tg3_primitive* prim = &mesh->primitives[i];
        if (find_attribute_accessor(prim, "TANGENT") >= 0) has_tangent = true;
        if (find_attribute_accessor(prim, "NORMAL") >= 0)  has_normal = true;
    }

    return vertex_type::POS_NORM_UV_COL_TANG_3D;
    /* TEST pbr needs full vertex for now
    if (has_tangent) return vertex_type::POS_NORM_UV_COL_TANG_3D;
    if (has_normal)  return vertex_type::POS_NORM_UV_COL_3D;
    return vertex_type::POS_3D;*/
}

const char* semantic_to_gltf_name(vertex_attribute_type semantic)
{
    switch (semantic) {
        case vertex_attribute_type::POSITION: return "POSITION";
        case vertex_attribute_type::NORMAL:   return "NORMAL";
        case vertex_attribute_type::TEXCOORD: return "TEXCOORD_0";
        case vertex_attribute_type::COLOR:    return "COLOR_0";
        case vertex_attribute_type::TANGENT:  return "TANGENT";
        default: return nullptr;
    }
}

void write_default_attribute_value(vertex_attribute_type semantic, void* dst, size_t size)
{
    std::memset(dst, 0, size);

    switch (semantic) {
        case vertex_attribute_type::NORMAL: {
            glm::vec3 n(0.0f, 1.0f, 0.0f);
            std::memcpy(dst, &n, std::min(size, sizeof(n)));
            break;
        }
        case vertex_attribute_type::COLOR: {
            glm::vec4 c(1.0f, 1.0f, 1.0f, 1.0f);
            std::memcpy(dst, &c, std::min(size, sizeof(c)));
            break;
        }
        case vertex_attribute_type::TANGENT: {
            glm::vec4 t(1.0f, 0.0f, 0.0f, 1.0f);
            std::memcpy(dst, &t, std::min(size, sizeof(t)));
            break;
        }
        default:
            break;
    }
}



// MikkTSpace Helpers

// Helper to resolve vertex index for indexed or non-indexed geometry
uint32_t get_vertex_index(const MikkMeshContext* mesh, int face, int vert_in_face) {
    size_t flat_idx = face * 3 + vert_in_face;
    if (flat_idx < mesh->ind_count) {
        return mesh->buf_ind[flat_idx];
    }
    return static_cast<uint32_t>(flat_idx);
}

int mikk_get_num_faces(const SMikkTSpaceContext* ctx) {
    auto* mesh = static_cast<const MikkMeshContext*>(ctx->m_pUserData);
    if (mesh->ind_count != 0) {
        return static_cast<int>(mesh->ind_count / 3);
    }
    return static_cast<int>(mesh->vert_count / 3);
}

int mikk_get_num_verts_of_face(const SMikkTSpaceContext* ctx, const int face) {
    return 3;
}

void mikk_get_position(const SMikkTSpaceContext* ctx, float pos_out[], const int face, const int vert) {
    auto* mesh = static_cast<const MikkMeshContext*>(ctx->m_pUserData);
    uint32_t v_idx = get_vertex_index(mesh, face, vert);
    float *pos = (float*) (mesh->pos_access.data + mesh->pos_access.stride*v_idx);
    pos_out[0] = pos[0];
    pos_out[1] = pos[1];
    pos_out[2] = pos[2];
}

void mikk_get_normal(const SMikkTSpaceContext* ctx, float norm_out[], const int face, const int vert) {
    auto* mesh = static_cast<const MikkMeshContext*>(ctx->m_pUserData);
    uint32_t v_idx = get_vertex_index(mesh, face, vert);
    float *norm = (float*) (mesh->norm_access.data + mesh->norm_access.stride*v_idx);
    norm_out[0] = norm[0];
    norm_out[1] = norm[1];
    norm_out[2] = norm[2];
}

void mikk_get_tex_coord(const SMikkTSpaceContext* ctx, float uv_out[], const int face, const int vert) {
    auto* mesh = static_cast<const MikkMeshContext*>(ctx->m_pUserData);
    uint32_t v_idx = get_vertex_index(mesh, face, vert);
    float *uv = (float*) (mesh->uv_access.data + mesh->uv_access.stride*v_idx);
    uv_out[0] = uv[0];
    uv_out[1] = uv[1];
}

void mikk_set_tspace_basic(const SMikkTSpaceContext* ctx, const float tangent[], const float sign, const int face, const int vert) {
    auto* mesh = static_cast<MikkMeshContext*>(ctx->m_pUserData);
    uint32_t v_idx = get_vertex_index(mesh, face, vert);
    float *tang = (float*) (mesh->tang_access.data + mesh->tang_access.stride*v_idx);
    tang[0] = tangent[0];
    tang[1] = tangent[1];
    tang[2] = tangent[2];
    tang[3] = sign;
}



// RTTI

void rmesh::_init_rtti()
{
    entt::meta_factory<rmesh>{}
    .type(entt::hashed_string{"rmesh"}.value())
    .custom<rtti::type_info>(rtti::type_info{
        .identifier = "mesh",
        .type_class = nb::rtti::TYPE_CLASS_RESOURCE,
        .data = {.resource = {.editor_icon = nullptr, .extensions = "",
            .factory_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource>
            {
                return std::make_shared<rmesh>(id);
            }
        }}
    })
    .func<&rmesh::tmp_add_to_default_scene>(entt::hashed_string{"add_to_scene"}.value())
    .custom<rtti::func_info>(rtti::func_info{.identifier = "add_to_scene"});

    rtti::res_ptr_registration<rmesh>("rmesh");
}



// TEST FUNCTION
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/components/mesh.hpp>
#include <newbase/components/spatial.hpp>
entt::entity rmesh::tmp_add_to_default_scene()
{
    cmesh comp = {
        .mesh = std::static_pointer_cast<rmesh>(shared_from_this()),
        .materials = {}
    };

    for(uint i = 0; i < m_mat_slots.size() && i < cmesh::MAX_MATERIALS; i++)
    {
        if(m_mat_slots[i] != entt::null_t{})
            comp.materials[i] = rman().load_sync<rmaterial>(m_mat_slots[i]);
    }
    auto &reg = engine::instance().default_scene().registry();
    auto eid = reg.create();
    reg.emplace<cmesh>(eid, comp);
    reg.emplace<cspatial>(eid, cspatial{});
    return eid;
}
