#include "newbase/render/shader.hpp"
#include <newbase/res/shader.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/nb_config.h>
#include <newbase/log.hpp>
#include <filesystem>

#ifdef NEWBASE_SLANG_RUNTIME
#include <slang.h>
#include <slang-com-ptr.h>
#include <mutex>
#include <iostream>

std::mutex _slang_global_session_mtx;
static slang::IGlobalSession* _slang_get_global_session();
static slang::ISession* _slang_get_thread_local_session();
bool compile_slang_to_spirv(
    const char* shader_source,
    const std::string &module_name,
    const std::string &file_name,
    const std::string& entry_point_name,
    nb::render::shader_stage stage,
    nb::rshader &dest) ;
static nb::render::shader_reflection extract_reflection_from_slang(
    slang::IComponentType* linked_program,
    nb::render::shader_stage stage,
    int target_index = 0);
// for flattening input structs
static void extract_varying_inputs(
    slang::VariableLayoutReflection* var_layout,
    nb::render::shader_reflection& reflection,
    uint32_t base_location = 0);
void dump_shader_reflection(const nb::render::shader_reflection& refl);
SlangStage stage_to_slang(nb::render::shader_stage stage)
{
    using shader_stage = nb::render::shader_stage;
    switch (stage) {
        case shader_stage::VERTEX:   return SlangStage::SLANG_STAGE_VERTEX;
        case shader_stage::FRAGMENT: return SlangStage::SLANG_STAGE_FRAGMENT;
        case shader_stage::COMPUTE:  return SlangStage::SLANG_STAGE_COMPUTE;
        default:                     return SlangStage::SLANG_STAGE_NONE;
    }
}
#endif

using namespace nb;


rshader::~rshader()
{
    // resource destruction callback, for the render system
    if(on_destroyed)
        on_destroyed(*this, on_destroyed_uptr);
}

bool rshader::do_load()
{
    log::info("[rshader] loading: %x", id());
    std::vector<char> data;
    if (!rman().read_all_sync(id(), data, true))
    {
        log::error("[rshader] surface: data loading failed: %x", id());
        return false;
    }

    // we need to understand what we are being asked to load
    // try to get the extension from the vfs node, if any
    const auto &vfs = rman().handles();
    const auto it = vfs.find(id());
    if(it == vfs.end())
    {
        log::error("[rshader] don't know what to do with shaders not in the vfs yet");
        return false;
    }

    const auto &name = it->second.name;
    auto path = std::filesystem::path{name};
    if(!path.has_extension())
    {
        log::error("[rshader] no extension: %s", path.c_str());
        return false;
    }

    if(path.extension() != ".slang")
    {
        log::error("[rshader] only slang for now: %s", path.extension().c_str());
        return false;
    }

    auto stage = render::shader_stage::VERTEX;
    if(path.string().find(".frag.") != std::string::npos)
        stage = render::shader_stage::FRAGMENT;
    else if(path.string().find(".comp.") != std::string::npos)
        stage = render::shader_stage::COMPUTE;

#ifdef NEWBASE_SLANG_RUNTIME
    if(!compile_slang_to_spirv(data.data(), name, name, "main", stage, *this))
    {
        log::error("[rshader] loading failed: %s", path.c_str());
        return false;
    }
    log::info("[rshader] loaded: '%s': reflection:", path.c_str());
    dump_shader_reflection(meta);

    return true;

#else
    log::error("[rshader] slang is not available!");
    return false;
#endif
}



// Slang session objects
#ifdef NEWBASE_SLANG_RUNTIME

bool compile_slang_to_spirv(const char *shader_source,
                            const std::string &module_name,
                            const std::string &file_name,
                            const std::string &entry_point_name,
                            nb::render::shader_stage stage, nb::rshader &dest)
{

  auto session = _slang_get_thread_local_session();  // TODO it is better to have one session per shader compilation
  auto s_stage = stage_to_slang(stage);

  // 4. Load the shader source string into a Module
  Slang::ComPtr<slang::IBlob> diagnostics_blob;
  Slang::ComPtr<slang::IModule> module;

  module = session->loadModuleFromSourceString(
      module_name.c_str(),           // Module name
      file_name.c_str(),     // Virtual file path
      shader_source, // Source code
      diagnostics_blob.writeRef());

  // Print compiler warnings/errors if any
  if (diagnostics_blob) {
      log::error("[slang] diagnostics: %s", diagnostics_blob->getBufferPointer());
      return false;
  }

  if (!module) {
    return false; // Compilation failed
  }

  // 5. Query the entry point (e.g. "vertexMain" or "fragmentMain")
  Slang::ComPtr<slang::IEntryPoint> entry_point;
  if (SLANG_FAILED(module->findEntryPointByName(entry_point_name.c_str(),
                                                entry_point.writeRef()))) {
    // Fallback: If not decorated with [shader("vertex")], explicitly check with
    // stage
    if (SLANG_FAILED(module->findAndCheckEntryPoint(
            entry_point_name.c_str(), s_stage, entry_point.writeRef(),
            diagnostics_blob.writeRef()))) {
        log::error("[slang] could not find entry point: %s", entry_point_name.c_str());
        if (diagnostics_blob) {
            log::error("[slang] diagnostics: %s", diagnostics_blob->getBufferPointer());
        }
      return false;
    }
  }

  // 6. Compose and Link Module + Entry Point
  slang::IComponentType *components[] = {module, entry_point};
  Slang::ComPtr<slang::IComponentType> program;

  if (SLANG_FAILED(session->createCompositeComponentType(
          components, 2, program.writeRef(), diagnostics_blob.writeRef()))) {
      log::error("[slang] failed to create composite component");
  if (diagnostics_blob) {
      log::error("[slang] diagnostics: %s", diagnostics_blob->getBufferPointer());
  }
    return false;
  }

  Slang::ComPtr<slang::IComponentType> linked_program;
  if (SLANG_FAILED(program->link(linked_program.writeRef(),
                                 diagnostics_blob.writeRef()))) {
      log::error("[slang] failed to link");
  if (diagnostics_blob) {
      log::error("[slang] diagnostics: %s", diagnostics_blob->getBufferPointer());
  }
    return false;
  }

  // 7. Extract the SPIR-V Bytecode Blob
  Slang::ComPtr<slang::IBlob> spirv_code;
  if (SLANG_FAILED(linked_program->getEntryPointCode(
          0, // entryPointIndex
          0, // targetIndex (0 = SPIR-V)
          spirv_code.writeRef(), diagnostics_blob.writeRef()))) {
      log::error("[slang] failed to get entry point 0");
  if (diagnostics_blob) {
      log::error("[slang] diagnostics: %s", diagnostics_blob->getBufferPointer());
  }
    return false;
  }

  // 8. Copy bytes into std::vector<uint8_t> for render::shader container
  const uint8_t *raw_bytes =
      static_cast<const uint8_t *>(spirv_code->getBufferPointer());
  size_t byte_count = spirv_code->getBufferSize();

  dest.format = render::shader_format::SPIRV;
  dest.data = {raw_bytes, raw_bytes + byte_count};
  dest.meta = extract_reflection_from_slang(linked_program, stage, 0);
  return true;
}

// TODO move to utility, expand shader data types as needed
static render::shader_data_type map_slang_type_to_data_type(slang::TypeReflection* type)
{
    using shader_data_type = render::shader_data_type;

    slang::TypeReflection::Kind kind = type->getKind();

    if (kind == slang::TypeReflection::Kind::Matrix) {
        return shader_data_type::MAT4X4; // 4x4 always
    }

    if (kind == slang::TypeReflection::Kind::Vector) {
        uint32_t element_count = type->getElementCount();
        slang::TypeReflection* scalar_type = type->getElementType();
        bool is_int = (scalar_type->getKind() == slang::TypeReflection::Kind::Scalar) &&
        (scalar_type->getScalarType() == slang::TypeReflection::ScalarType::Int32 ||
        scalar_type->getScalarType() == slang::TypeReflection::ScalarType::UInt32);

        if (is_int) {
            switch (element_count) {
                case 1: return shader_data_type::INT1;
                case 2: return shader_data_type::INT2;
                case 3: return shader_data_type::INT3;
                case 4: return shader_data_type::INT4;
            }
        } else {
            switch (element_count) {
                case 1: return shader_data_type::FLOAT1;
                case 2: return shader_data_type::FLOAT2;
                case 3: return shader_data_type::FLOAT3;
                case 4: return shader_data_type::FLOAT4;
            }
        }
    }

    if (kind == slang::TypeReflection::Kind::Scalar) {
        if (type->getScalarType() == slang::TypeReflection::ScalarType::Int32 ||
            type->getScalarType() == slang::TypeReflection::ScalarType::UInt32) {
            return shader_data_type::INT1;
            }
            return shader_data_type::FLOAT1;
    }

    return shader_data_type::FLOAT1;
}

render::shader_reflection extract_reflection_from_slang(
    slang::IComponentType* linked_program,
    render::shader_stage stage,
    int target_index)
{
    render::shader_reflection reflection;
    reflection.stage = stage;

    slang::ProgramLayout* layout = linked_program->getLayout(target_index);
    if (!layout)
        return reflection;

    // GLOBALS
    unsigned global_param_count = layout->getParameterCount();
    for (unsigned i = 0; i < global_param_count; ++i)
    {
        slang::VariableLayoutReflection* var_layout = layout->getParameterByIndex(i);
        slang::TypeLayoutReflection* type_layout = var_layout->getTypeLayout();
        slang::TypeReflection::Kind kind = type_layout->getKind();

        const char* var_name = var_layout->getName();
        std::string name = var_name ? var_name : "";

        // Uniform Buffers (cbuffers)
        if (kind == slang::TypeReflection::Kind::ConstantBuffer)
        {
            render::shader_uniform_buffer_desc ub_desc;
            ub_desc.name = name;
            ub_desc.slot = var_layout->getBindingIndex();

            // Get the element type size (the struct inside the cbuffer)
            slang::TypeLayoutReflection* container_type = type_layout->getElementTypeLayout();
            ub_desc.size_bytes = static_cast<uint32_t>(container_type->getSize());

            // Iterate over struct members inside the cbuffer
            unsigned member_count = container_type->getFieldCount();
            for (unsigned m = 0; m < member_count; ++m)
            {
                slang::VariableLayoutReflection* field = container_type->getFieldByIndex(m);
                slang::TypeLayoutReflection* field_type = field->getTypeLayout();

                render::shader_uniform_member member;
                const char* field_name = field->getName();
                member.name = field_name ? field_name : "";
                member.offset = static_cast<uint32_t>(field->getOffset());
                member.size = static_cast<uint32_t>(field_type->getSize());
                member.type = map_slang_type_to_data_type(field_type->getType());

                // Check for array uniforms
                if (field_type->getKind() == slang::TypeReflection::Kind::Array) {
                    member.array_element_count = static_cast<uint32_t>(field_type->getElementCount());
                } else {
                    member.array_element_count = 1;
                }

                ub_desc.members.push_back(member);
            }

            reflection.uniform_buffers.push_back(ub_desc);
            reflection.num_uniform_buffers++;
        }
        // Check for Combined Texture-Samplers or Sampler States
        else if (kind == slang::TypeReflection::Kind::SamplerState ||
            kind == slang::TypeReflection::Kind::TextureBuffer ||
            kind == slang::TypeReflection::Kind::Resource)
        {
            // Check if it's a read-write storage resource vs standard sampled texture
            auto access = type_layout->getResourceAccess();
            bool is_read_write = (access == SLANG_RESOURCE_ACCESS_READ_WRITE ||
            access == SLANG_RESOURCE_ACCESS_WRITE);

            if (is_read_write)
            {
                reflection.num_storage_textures++;
            }
            else
            {
                render::shader_texture_binding_desc tex_desc;
                tex_desc.name = var_name;
                tex_desc.slot = var_layout->getBindingIndex();
                tex_desc.is_sampler = true;

                reflection.textures.push_back(tex_desc);
                reflection.num_samplers++;
            }
        }
        // --- Storage Buffers (SSBOs / StructuredBuffers) ---
        else if (kind == slang::TypeReflection::Kind::ShaderStorageBuffer)
        {
            reflection.num_storage_buffers++;
        }
    }

    // VERTEX ATTRIBUTES
    if (stage != render::shader_stage::VERTEX)
        return reflection; // return early if not vert

    unsigned entry_point_count = layout->getEntryPointCount();
    for (unsigned e = 0; e < entry_point_count; ++e)
    {
        slang::EntryPointLayout* entry_point = layout->getEntryPointByIndex(e);
        if (entry_point->getStage() == SLANG_STAGE_VERTEX)
        {
            unsigned param_count = entry_point->getParameterCount();
            for (unsigned p = 0; p < param_count; ++p)
            {
                slang::VariableLayoutReflection* param = entry_point->getParameterByIndex(p);

                // We are looking for varying stage inputs (POSITION, COLOR, TEXCOORD)
                if (param->getCategory() == slang::ParameterCategory::VaryingInput)
                {
                    // The base location of the parameter itself
                    uint32_t base_location = static_cast<uint32_t>(param->getBindingIndex());

                    // Use our new struct-flattening helper
                    extract_varying_inputs(param, reflection, base_location);
                }
            }

            break;  // only a single vertex stage entry point is read
        }
    }

    return reflection;
}


void extract_varying_inputs(
    slang::VariableLayoutReflection* var_layout,
    render::shader_reflection& reflection,
    uint32_t base_location)
{
    using namespace render;
    slang::TypeLayoutReflection* type_layout = var_layout->getTypeLayout();

    // If it's a struct, iterate through its fields and recurse
    if (type_layout->getKind() == slang::TypeReflection::Kind::Struct)
    {
        unsigned field_count = type_layout->getFieldCount();
        for (unsigned i = 0; i < field_count; ++i)
        {
            slang::VariableLayoutReflection* field = type_layout->getFieldByIndex(i);

            // Get the field's relative location offset for vertex inputs
            uint32_t relative_loc = static_cast<uint32_t>(
                field->getOffset(slang::ParameterCategory::VaryingInput));

            extract_varying_inputs(field, reflection, base_location + relative_loc);
        }
    }
    else
    {
        // We hit a primitive type (float2, float4, etc.)
        shader_vertex_attribute_desc attr;

        // Grab the field name (e.g., "pos", "color")
        const char* name = var_layout->getName();
        attr.name = name ? name : "unknown";

        attr.location = base_location;
        attr.type = map_slang_type_to_data_type(type_layout->getType());

        reflection.attributes.push_back(attr);
    }
}


slang::IGlobalSession* _slang_get_global_session()
{
    static Slang::ComPtr<slang::IGlobalSession> global_session = [](){
        Slang::ComPtr<slang::IGlobalSession> ptr;
        slang::createGlobalSession(ptr.writeRef());
        return ptr;
    }();
    return global_session.get();
}

slang::ISession* _slang_get_thread_local_session()
{
    thread_local Slang::ComPtr<slang::ISession> t_session {};
    slang::IGlobalSession *g_session = _slang_get_global_session();

    if (!t_session)
    {
        auto global = _slang_get_global_session();
        slang::TargetDesc target_desc = {};
        target_desc.format = SLANG_SPIRV;
        target_desc.profile = g_session->findProfile("spirv_1_0");

        slang::SessionDesc session_desc = {};
        session_desc.targets = &target_desc;
        session_desc.targetCount = 1;

        // ISession is created from the thread-safe IGlobalSession
        global->createSession(session_desc, t_session.writeRef());
    }

    return t_session.get();
}


/// DUMP METHODS
using namespace render;

// Helper to stringify shader stages
inline const char* to_string(shader_stage stage) {
    switch (stage) {
        case shader_stage::VERTEX:   return "VERTEX";
        case shader_stage::FRAGMENT: return "FRAGMENT";
        case shader_stage::COMPUTE:  return "COMPUTE";
        default:                     return "UNKNOWN";
    }
}

// Helper to stringify data types
inline const char* to_string(shader_data_type type) {
    switch (type) {
        case shader_data_type::FLOAT1: return "FLOAT1";
        case shader_data_type::FLOAT2: return "FLOAT2";
        case shader_data_type::FLOAT3: return "FLOAT3";
        case shader_data_type::FLOAT4: return "FLOAT4";
        case shader_data_type::INT1:   return "INT1";
        case shader_data_type::INT2:   return "INT2";
        case shader_data_type::INT3:   return "INT3";
        case shader_data_type::INT4:   return "INT4";
        case shader_data_type::MAT4X4: return "MAT4X4";
        default:                       return "UNKNOWN";
    }
}

// Main dumping function
void dump_shader_reflection(const shader_reflection& refl) {
    log::info("=== Shader Reflection Dump ===");
    log::info("Stage: %s", to_string(refl.stage));
    log::info("Resource Counts: %u samplers, %u uniform buffers, %u storage textures, %u storage buffers",
              refl.num_samplers, refl.num_uniform_buffers, refl.num_storage_textures, refl.num_storage_buffers);

    // Dump Vertex Attributes (if any)
    if (!refl.attributes.empty()) {
        log::info("--- Vertex Attributes (%zu) ---", refl.attributes.size());
        for (const auto& attr : refl.attributes) {
            log::info("  Location %u: %s (Type: %s)",
                      attr.location, attr.name.c_str(), to_string(attr.type));
        }
    }

    // Dump Uniform Buffers
    if (!refl.uniform_buffers.empty()) {
        log::info("--- Uniform Buffers (%zu) ---", refl.uniform_buffers.size());
        for (const auto& ub : refl.uniform_buffers) {
            log::info("  Buffer '%s' (Slot: %u, Total Size: %u bytes)",
                      ub.name.c_str(), ub.slot, ub.size_bytes);

            for (const auto& member : ub.members) {
                // Formatting array suffix if it's an array
                std::string array_suffix = (member.array_element_count > 1)
                ? "[" + std::to_string(member.array_element_count) + "]"
                : "";

                log::info("    [Offset: %3u | Size: %3u] %s%s : %s",
                          member.offset, member.size,
                          member.name.c_str(), array_suffix.c_str(), to_string(member.type));
            }
        }
    }

    // Dump Textures
    if (!refl.textures.empty()) {
        log::info("--- Textures (%zu) ---", refl.textures.size());
        for (const auto& tex : refl.textures) {
            log::info("  Slot %u: %s (Is Sampler: %s)",
                      tex.slot, tex.name.c_str(), tex.is_sampler ? "true" : "false");
        }
    }

    log::info("==============================");
}




#endif
