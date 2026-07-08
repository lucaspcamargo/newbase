#include <newbase/mcp/mcp.hpp>
#include <newbase/log.hpp>
#include <newbase/engine.hpp>
#include <newbase/scene.hpp>
#include <newbase/components/structure.hpp>
#include <newbase/reflection/contexts.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/utility/glm.hpp>
#include <newbase/res/resource.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/script_lua/script_lua.hpp>
#include <entt/entt.hpp>
#include <httplib.h>
#include <ryml.hpp>
#include <ryml_std.hpp>
#include <c4/format.hpp>

using namespace nb;
using entt::operator""_hs;

namespace {

// ryml's default error handler calls abort() on malformed input (verified
// empirically — it is not a recoverable/catchable path by default). Fine for
// trusted local files, unacceptable for a network-facing endpoint: install a
// throwing error callback for the duration of this parse so callers can
// catch it and return a JSON-RPC error instead of crashing the engine.
//
// Note: this constructs its own Tree/Parser with custom Callbacks rather than
// touching ryml's global default callbacks — so, unlike a global-state
// approach, this is not a source of cross-thread interference with any other
// concurrent ryml parse elsewhere in the engine (e.g. resource loading on the
// main thread while an MCP request is being parsed on an httplib thread).
[[noreturn]] void _throwing_ryml_error(const char* msg, size_t len, ryml::Location, void*)
{
    throw std::runtime_error(std::string(msg, len));
}

ryml::Tree parse_json_safe(const std::string& text)
{
    ryml::Callbacks defaults = ryml::get_callbacks();
    ryml::Callbacks throwing(defaults.m_user_data, defaults.m_allocate, defaults.m_free, &_throwing_ryml_error);

    ryml::Tree tree(throwing);
    ryml::Parser::handler_type evt_handler(throwing);
    ryml::Parser parser(&evt_handler);
    ryml::parse_json_in_arena(&parser, c4::to_csubstr(text), &tree);
    return tree;
}

// Re-serializes a node's VALUE (not its key) back to a JSON text fragment —
// used to round-trip "id"/"params"/"arguments" without hand-rolling
// type-aware quoting. Plain ryml::emitrs_json(node) on a keyed child node
// emits it as a "key": value pair (verified empirically), which produces
// invalid JSON once embedded as a standalone value elsewhere — so scalars go
// through a keyless root, and maps/seqs get their children duplicated into a
// fresh keyless root of the same container type.
std::string node_to_json(ryml::ConstNodeRef node)
{
    if (node.has_val())
    {
        if (node.val_is_null())
            return "null";
        if (node.is_val_quoted())
        {
            ryml::Tree t;
            t.rootref() << node.val();
            return ryml::emitrs_json<std::string>(t);
        }
        return std::string(node.val().str, node.val().len);
    }

    ryml::Tree dst;
    ryml::NodeRef droot = dst.rootref();
    droot |= node.is_map() ? ryml::MAP : ryml::SEQ;
    dst.duplicate_children(node.tree(), node.id(), dst.root_id(), ryml::NONE);
    return ryml::emitrs_json<std::string>(dst);
}

std::string rpc_result(const std::string& id_json, const std::string& result_json)
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id_json + ",\"result\":" + result_json + "}";
}

// Serializes an arbitrary raw string as a JSON string literal (quotes +
// escaping included) — reuses ryml's JSON emitter's own scalar-quoting logic
// rather than hand-rolling escaping.
std::string json_string(const std::string& s)
{
    ryml::Tree t;
    t.rootref() << c4::to_csubstr(s);
    return ryml::emitrs_json<std::string>(t);
}

std::string rpc_error(const std::string& id_json, int code, const std::string& message)
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id_json + ",\"error\":{\"code\":" +
           std::to_string(code) + ",\"message\":" + json_string(message) + "}}";
}

// Reads an entity/id-like integer field regardless of whether the client
// sent it as a JSON number or a JSON string (some MCP clients stringify
// arguments since our inputSchema doesn't declare per-field types) — ryml's
// typed `>>` deserialization does not tolerate a quoted numeric value, so
// parse the raw text ourselves instead of relying on it.
bool read_uint32(ryml::ConstNodeRef node, uint32_t* out)
{
    if (!node.has_val())
        return false;
    std::string text(node.val().str, node.val().len);
    try
    {
        size_t consumed = 0;
        unsigned long v = std::stoul(text, &consumed);
        if (consumed != text.size())
            return false;
        *out = static_cast<uint32_t>(v);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

// Locates the live component instance for {entity, component}, wrapped as an
// entt::meta_any that REFERENCES the original storage memory (not a copy) —
// via entt::meta_type::from_void, mirroring editor.cpp's inspector panel
// (editor.cpp:317-328). Returns an empty (falsy) meta_any on failure and sets
// *out_error.
entt::meta_any find_component_ref(entt::registry& reg, entt::entity eid, const std::string& component_name, std::string* out_error)
{
    if (!reg.valid(eid))
    {
        *out_error = "no such entity";
        return {};
    }
    for (auto&& curr : reg.storage())
    {
        auto& storage = curr.second;
        if (!storage.contains(eid))
            continue;
        auto comp_type = entt::resolve(curr.first);
        if (comp_type.info() == entt::type_id<void>())
            continue;
        const rtti::type_info* info = comp_type.custom();
        if (!info || info->type_class != rtti::TYPE_CLASS_COMPONENT)
            continue;
        if (component_name != info->identifier.c_str())
            continue;
        void* void_val = storage.value(eid);
        return comp_type.from_void(void_val);
    }
    *out_error = "no such component on entity";
    return {};
}

// Finds the entt::meta_data for a named TOP-LEVEL field of a component's
// meta_any. Deliberately flat — v1 of get_field/set_field does not walk into
// nested struct fields.
entt::meta_data find_field(entt::meta_any& comp_ref, const std::string& field_name)
{
    for (auto&& [did, d] : comp_ref.type().data())
    {
        const rtti::data_info* di = d.custom().operator const rtti::data_info*();
        if (di && field_name == di->identifier.c_str())
            return d;
    }
    return {};
}

// Serializes a field's current value to a JSON fragment. Covers the same
// concrete type set as meta_any_editor.cpp's generic editor (bool/float/int/
// unsigned int/glm vec2-4/quat/entt::entity/std::string), plus resource_ptr
// fields (resolved to their VFS path if known, else their raw hash id).
// Returns an empty string for any other (e.g. nested struct) field type —
// callers treat that as "unsupported".
std::string field_to_json(entt::meta_any& member)
{
    auto ti = member.type().info();
    if (ti == entt::type_id<void>())
        return "null";
    if (ti == entt::type_id<bool>())
        return *member.try_cast<bool>() ? "true" : "false";
    if (ti == entt::type_id<float>())
        return std::to_string(*member.try_cast<float>());
    if (ti == entt::type_id<int>())
        return std::to_string(*member.try_cast<int>());
    if (ti == entt::type_id<unsigned int>())
        return std::to_string(*member.try_cast<unsigned int>());
    if (ti == entt::type_id<entt::entity>())
        return std::to_string(entt::to_integral(*member.try_cast<entt::entity>()));
    if (ti == entt::type_id<std::string>())
        return json_string(*member.try_cast<std::string>());
    if (ti == entt::type_id<glm::vec2>())
    {
        auto v = *member.try_cast<glm::vec2>();
        return "{\"x\":" + std::to_string(v.x) + ",\"y\":" + std::to_string(v.y) + "}";
    }
    if (ti == entt::type_id<glm::vec3>())
    {
        auto v = *member.try_cast<glm::vec3>();
        return "{\"x\":" + std::to_string(v.x) + ",\"y\":" + std::to_string(v.y) +
               ",\"z\":" + std::to_string(v.z) + "}";
    }
    if (ti == entt::type_id<glm::vec4>())
    {
        auto v = *member.try_cast<glm::vec4>();
        return "{\"x\":" + std::to_string(v.x) + ",\"y\":" + std::to_string(v.y) +
               ",\"z\":" + std::to_string(v.z) + ",\"w\":" + std::to_string(v.w) + "}";
    }
    if (ti == entt::type_id<glm::quat>())
    {
        auto v = *member.try_cast<glm::quat>();
        return "{\"x\":" + std::to_string(v.x) + ",\"y\":" + std::to_string(v.y) +
               ",\"z\":" + std::to_string(v.z) + ",\"w\":" + std::to_string(v.w) + "}";
    }

    const rtti::type_info* type_rtti = member.type().custom();
    if (type_rtti && type_rtti->type_class == rtti::TYPE_CLASS_RESOURCE_PTR)
    {
        auto ptr = type_rtti->data.resource_ptr.get_ptr(member);
        if (!ptr)
            return "null";
        auto& handles = rman().handles();
        auto it = handles.find(ptr->id());
        if (it != handles.end() && !it->second.path.empty())
            return json_string(it->second.path);
        return std::to_string(ptr->id());
    }

    return std::string();
}

// Parses a JSON value node into an already-typed `target` meta_any (e.g. one
// obtained from entt::meta_data::get, or a default-constructed placeholder
// of a function argument's type). Same type coverage as field_to_json. For
// resource_ptr-typed targets, the JSON value may be either a string (a VFS
// resource path, hashed via entt::hashed_string — the same idiom used
// throughout resource loading, e.g. res/loaders.cpp) or an integer (a
// resource hash id used directly), or null (clears the pointer). Returns
// false and sets *out_error on any failure (unknown/unsupported type,
// malformed value, or unresolvable resource). Shared by entity_field_set and
// sys_function_call (per-argument conversion).
bool assign_json_to_meta_any(entt::meta_any& target, ryml::ConstNodeRef valnode, std::string* out_error)
{
    // Some MCP clients JSON-stringify object/array-typed arguments when the
    // tool's inputSchema doesn't declare their type (observed: {"x":1,"y":2}
    // arriving as the literal string "{\"x\":1,\"y\":2}") — same root cause
    // as entity ids arriving as quoted numbers (see read_uint32). If the
    // value looks like a stringified object/array, re-parse it before use.
    // A genuine string-valued field (e.g. std::string) never starts with
    // '{'/'[', so this cannot misfire on those.
    ryml::Tree value_holder;
    if (valnode.has_val() && valnode.is_val_quoted())
    {
        std::string inner(valnode.val().str, valnode.val().len);
        if (!inner.empty() && (inner.front() == '{' || inner.front() == '['))
        {
            try
            {
                value_holder = parse_json_safe(inner);
                valnode = value_holder.rootref();
            }
            catch (const std::exception&)
            {
                // leave valnode as-is; downstream parsing will fail with a clear error
            }
        }
    }

    auto ti = target.type().info();

    if (ti == entt::type_id<bool>())
    {
        bool v; valnode >> v; target.assign(v);
    }
    else if (ti == entt::type_id<float>())
    {
        float v; valnode >> v; target.assign(v);
    }
    else if (ti == entt::type_id<int>())
    {
        int v; valnode >> v; target.assign(v);
    }
    else if (ti == entt::type_id<unsigned int>())
    {
        unsigned int v; valnode >> v; target.assign(v);
    }
    else if (ti == entt::type_id<entt::entity>())
    {
        uint32_t v; valnode >> v; target.assign(static_cast<entt::entity>(v));
    }
    else if (ti == entt::type_id<std::string>())
    {
        std::string v; valnode >> v; target.assign(v);
    }
    else if (ti == entt::type_id<glm::vec2>())
    {
        glm::vec2 v{};
        if (valnode.has_child("x")) valnode["x"] >> v.x;
        if (valnode.has_child("y")) valnode["y"] >> v.y;
        target.assign(v);
    }
    else if (ti == entt::type_id<glm::vec3>())
    {
        glm::vec3 v{};
        if (valnode.has_child("x")) valnode["x"] >> v.x;
        if (valnode.has_child("y")) valnode["y"] >> v.y;
        if (valnode.has_child("z")) valnode["z"] >> v.z;
        target.assign(v);
    }
    else if (ti == entt::type_id<glm::vec4>())
    {
        glm::vec4 v{};
        if (valnode.has_child("x")) valnode["x"] >> v.x;
        if (valnode.has_child("y")) valnode["y"] >> v.y;
        if (valnode.has_child("z")) valnode["z"] >> v.z;
        if (valnode.has_child("w")) valnode["w"] >> v.w;
        target.assign(v);
    }
    else if (ti == entt::type_id<glm::quat>())
    {
        glm::quat v{};
        if (valnode.has_child("x")) valnode["x"] >> v.x;
        if (valnode.has_child("y")) valnode["y"] >> v.y;
        if (valnode.has_child("z")) valnode["z"] >> v.z;
        if (valnode.has_child("w")) valnode["w"] >> v.w;
        target.assign(v);
    }
    else
    {
        const rtti::type_info* type_rtti = target.type().custom();
        if (!type_rtti || type_rtti->type_class != rtti::TYPE_CLASS_RESOURCE_PTR)
        {
            *out_error = "unsupported field type";
            return false;
        }

        if (valnode.val_is_null())
        {
            type_rtti->data.resource_ptr.set_ptr(target, nullptr);
        }
        else
        {
            entt::id_type hash_id = 0;
            if (valnode.is_val_quoted())
            {
                std::string path;
                valnode >> path;
                hash_id = entt::hashed_string{path.c_str()}.value();
            }
            else
            {
                valnode >> hash_id;
            }

            auto ptr = rman().get(type_rtti->data.resource_ptr.resource_type_id, hash_id);
            if (!ptr)
            {
                *out_error = "resource not found";
                return false;
            }
            type_rtti->data.resource_ptr.set_ptr(target, ptr);
        }
    }

    return true;
}

// Parses a JSON value node into field `d` of component `comp_ref`.
bool json_to_field(entt::meta_data d, entt::meta_any& comp_ref, ryml::ConstNodeRef valnode, std::string* out_error)
{
    auto member = d.get(comp_ref);
    if (!assign_json_to_meta_any(member, valnode, out_error))
        return false;
    d.set(comp_ref, member);
    return true;
}

// Constructs a default placeholder value of the given meta_type, later
// overwritten in place by assign_json_to_meta_any — used to build
// sys_function_call's argument list. Explicitly covers the same primitive/
// glm type set as field_to_json/assign_json_to_meta_any (no dependency on a
// registered meta default-ctor for those); anything else (e.g. resource_ptr,
// whose shared_ptr<T> does have a registered default ctor) falls back to
// entt::meta_type::construct(), which returns a falsy meta_any if no
// default ctor is registered.
entt::meta_any default_of_type(entt::meta_type type)
{
    auto ti = type.info();
    if (ti == entt::type_id<bool>())         return entt::meta_any{bool{}};
    if (ti == entt::type_id<float>())        return entt::meta_any{float{}};
    if (ti == entt::type_id<int>())          return entt::meta_any{int{}};
    if (ti == entt::type_id<unsigned int>()) return entt::meta_any{(unsigned int){}};
    if (ti == entt::type_id<entt::entity>()) return entt::meta_any{entt::entity{entt::null}};
    if (ti == entt::type_id<std::string>())  return entt::meta_any{std::string{}};
    if (ti == entt::type_id<glm::vec2>())    return entt::meta_any{glm::vec2{}};
    if (ti == entt::type_id<glm::vec3>())    return entt::meta_any{glm::vec3{}};
    if (ti == entt::type_id<glm::vec4>())    return entt::meta_any{glm::vec4{}};
    if (ti == entt::type_id<glm::quat>())    return entt::meta_any{glm::quat{}};
    return type.construct();
}

// Resolves an RTTI-registered, currently-running system by its identifier —
// same filter/lookup idiom as sys_list and script_lua's bind_systems
// (script_lua.cpp:217-238). Writes the live instance pointer (as obtained
// from engine::instance().system_from_id(...).get(), matching how
// script_lua passes it to entt::meta_type::from_void) to *out_instance.
// Returns an empty (falsy) meta_type on failure and sets *out_error.
entt::meta_type find_system_type(const std::string& name, void** out_instance, std::string* out_error)
{
    auto system_t = entt::resolve<nb::system>();
    for (auto&& [cpp_id, type] : entt::resolve())
    {
        if (!type.can_cast(system_t))
            continue;
        const rtti::type_info* info = type.custom();
        if (!info || info->type_class != rtti::TYPE_CLASS_SYSTEM)
            continue;
        if (name != info->identifier.c_str())
            continue;

        auto sys = engine::instance().system_from_id(type.id());
        if (!sys)
        {
            *out_error = "system is registered but not currently running";
            return {};
        }
        *out_instance = sys.get();
        return type;
    }
    *out_error = "no such system";
    return {};
}

// Finds the entt::meta_func for a named function on a system's meta_type —
// mirrors find_field's by-identifier lookup, since rtti::func_info (like
// rtti::data_info) only carries a display/lookup name, not a stable hash.
entt::meta_func find_func(entt::meta_type type, const std::string& func_name)
{
    for (auto&& [fhash, f] : type.func())
    {
        const rtti::func_info* fi = f.custom();
        if (fi && func_name == fi->identifier.c_str())
            return f;
    }
    return {};
}

}

mcp::mcp()
{
    log::info("[mcp] constructing");
}

mcp::~mcp()
{
    log::info("[mcp] destroying");
    if (_svr)
    {
        _svr->stop();
        if (_svr_thread.joinable())
            _svr_thread.join();
    }
    log::info("[mcp] destroyed");
}

bool mcp::init(ryml::ConstNodeRef cfg)
{
    log::info("[mcp] init");

    if (!cfg.invalid() && !cfg.empty() && cfg.has_child("port"))
        cfg["port"] >> _port;

    _register_builtin_tools();

    _svr = std::make_unique<httplib::Server>();
    _svr->Post("/", [this](const httplib::Request& req, httplib::Response& res) {
        std::string body = _dispatch_rpc(req.body);
        if (body.empty())
        {
            res.status = 204;
            return;
        }
        res.set_content(body, "application/json");
    });

    _svr_thread = std::thread([this]() {
        log::info("[mcp] listening on 127.0.0.1:%d", _port);
        if (!_svr->listen("127.0.0.1", _port))
            log::error("[mcp] failed to bind 127.0.0.1:%d", _port);
    });

    return true;
}

bool mcp::step(step_phase phase)
{
    if (phase != step_phase::POST_UPDATE)
        return true;

    std::deque<std::shared_ptr<pending_call>> batch;
    {
        std::lock_guard<std::mutex> lock(_pending_mtx);
        batch.swap(_pending);
    }

    for (auto& call : batch)
    {
        auto it = _tools.find(call->tool_name);
        if (it == _tools.end())
        {
            call->result.set_value(std::string());
            continue;
        }
        log::info("[mcp] tool call: %s(%s)", call->tool_name.c_str(), call->args_json.c_str());

        // Tool bodies only guard their own JSON parsing with try/catch; value
        // extraction after that (e.g. `aroot["entity"] >> raw_id`) can still
        // throw on a malformed/mistyped argument, since the tree carries our
        // throwing ryml callbacks (see parse_json_safe). An uncaught
        // exception here would unwind straight through the main loop and
        // terminate the engine — so catch broadly at the dispatch boundary
        // and report it as a normal tool error instead.
        std::string out;
        try
        {
            out = it->second.fn(call->args_json);
        }
        catch (const std::exception& e)
        {
            log::warn("[mcp] tool '%s' threw: %s", call->tool_name.c_str(), e.what());
            out = std::string("{\"error\":") + json_string(std::string("internal error: ") + e.what()) + "}";
        }

        if (out.rfind("{\"error\"", 0) == 0)
            log::warn("[mcp] tool '%s' returned an error: %s", call->tool_name.c_str(), out.c_str());

        call->result.set_value(out);
    }

    return true;
}

bool mcp::event(SDL_Event*)
{
    return true;
}

void mcp::register_tool(std::string name, std::string description, std::string input_schema_json, tool_fn fn)
{
    _tools[name] = tool_entry{std::move(description), std::move(input_schema_json), std::move(fn)};
}

void mcp::_register_builtin_tools()
{
    register_tool("ping",
        "Replies with pong. Useful to verify the MCP bridge and the main-thread round trip are working.",
        "{\"type\":\"object\",\"properties\":{}}",
        [](const std::string&) { return std::string("{\"message\":\"pong\"}"); });

    register_tool("sys_list",
        "Lists all RTTI-registered systems and whether each is currently instantiated/running.",
        "{\"type\":\"object\",\"properties\":{}}",
        [](const std::string&) -> std::string {
            ryml::Tree out;
            ryml::NodeRef oroot = out.rootref();
            oroot |= ryml::MAP;
            auto seq = oroot.append_child();
            seq << ryml::key("systems");
            seq |= ryml::SEQ;

            auto system_t = entt::resolve<nb::system>();
            for (auto&& [cpp_id, type] : entt::resolve())
            {
                if (!type.can_cast(system_t))
                    continue;
                const rtti::type_info* info = type.custom();
                if (!info || info->type_class != rtti::TYPE_CLASS_SYSTEM)
                    continue;

                auto sys = engine::instance().system_from_id(type.id());

                auto item = seq.append_child();
                item |= ryml::MAP;
                item.append_child() << ryml::key("name") << info->identifier.c_str();
                item.append_child() << ryml::key("running") << (sys != nullptr ? "true" : "false");
            }

            return ryml::emitrs_json<std::string>(out);
        });

    register_tool("entity_list",
        "Lists all entities in a scene, with their name (if any) and parent entity id (if any). "
        "Argument: {\"scene_id\": <optional string; omitted/empty = default scene>}. "
        "Only the default scene exists today, but scene_id is accepted for forward compatibility.",
        "{\"type\":\"object\",\"properties\":{\"scene_id\":{\"type\":\"string\"}}}",
        [](const std::string& args_json) -> std::string {
            ryml::ConstNodeRef aroot;
            ryml::Tree atree;
            try
            {
                atree = parse_json_safe(args_json);
                aroot = atree.rootref();
            }
            catch (const std::exception& e)
            {
                return std::string("{\"error\":") + json_string(std::string("invalid arguments: ") + e.what()) + "}";
            }

            std::string scene_id_str;
            if (aroot.has_child("scene_id"))
                aroot["scene_id"] >> scene_id_str;

            nb::scene* sc = scene_id_str.empty()
                ? &engine::instance().default_scene()
                : engine::instance().find_scene(entt::hashed_string{scene_id_str.c_str()}.value());
            if (!sc)
                return "{\"error\":\"no such scene\"}";

            auto& reg = sc->registry();

            ryml::Tree out;
            ryml::NodeRef oroot = out.rootref();
            oroot |= ryml::MAP;
            auto seq = oroot.append_child();
            seq << ryml::key("entities");
            seq |= ryml::SEQ;

            for (auto id : reg.view<entt::entity>())
            {
                auto item = seq.append_child();
                item |= ryml::MAP;
                item.append_child() << ryml::key("entity") << entt::to_integral(id);

                if (auto* s = reg.try_get<cstructure>(id))
                {
                    if (s->has_name())
                        item.append_child() << ryml::key("name") << s->get_name();
                    if (s->parent != entt::null)
                        item.append_child() << ryml::key("parent") << entt::to_integral(s->parent);
                }
            }

            return ryml::emitrs_json<std::string>(out);
        });

    register_tool("entity_component_list",
        "Lists the RTTI-registered components attached to an entity in the default scene. "
        "Argument: {\"entity\": <integer entity id>}.",
        "{\"type\":\"object\",\"properties\":{\"entity\":{\"type\":\"integer\"}},\"required\":[\"entity\"]}",
        [](const std::string& args_json) -> std::string {
            ryml::ConstNodeRef aroot;
            ryml::Tree atree;
            try
            {
                atree = parse_json_safe(args_json);
                aroot = atree.rootref();
            }
            catch (const std::exception& e)
            {
                return std::string("{\"error\":") + json_string(std::string("invalid arguments: ") + e.what()) + "}";
            }

            uint32_t raw_id = 0;
            if (!aroot.has_child("entity") || !read_uint32(aroot["entity"], &raw_id))
                return "{\"error\":\"missing or malformed 'entity' argument\"}";
            auto eid = static_cast<entt::entity>(raw_id);

            auto& reg = engine::instance().default_scene().registry();
            if (!reg.valid(eid))
                return "{\"error\":\"no such entity\"}";

            ryml::Tree out;
            ryml::NodeRef oroot = out.rootref();
            oroot |= ryml::MAP;
            oroot.append_child() << ryml::key("entity") << raw_id;
            auto comps = oroot.append_child();
            comps << ryml::key("components");
            comps |= ryml::SEQ;

            for (auto&& curr : reg.storage())
            {
                auto& storage = curr.second;
                if (!storage.contains(eid))
                    continue;
                auto comp_type = entt::resolve(curr.first);
                if (comp_type.info() == entt::type_id<void>())
                    continue;
                rtti::type_info* info = comp_type.custom();
                if (!info || info->type_class != rtti::TYPE_CLASS_COMPONENT)
                    continue;

                auto item = comps.append_child();
                item |= ryml::MAP;
                item.append_child() << ryml::key("name") << info->identifier.c_str();
            }

            return ryml::emitrs_json<std::string>(out);
        });

    register_tool("entity_field_get",
        "Reads a single top-level field of a component on an entity in the default scene. "
        "Argument: {\"entity\": <integer entity id>, \"component\": <string>, \"field\": <string>}. "
        "Nested/struct fields are not supported (v1 is flat-only). Resource-pointer fields "
        "resolve to their VFS path (or their raw hash id if the path is unknown), or null if unset.",
        "{\"type\":\"object\",\"properties\":{"
        "\"entity\":{\"type\":\"integer\"},"
        "\"component\":{\"type\":\"string\"},"
        "\"field\":{\"type\":\"string\"}"
        "},\"required\":[\"entity\",\"component\",\"field\"]}",
        [](const std::string& args_json) -> std::string {
            ryml::ConstNodeRef aroot;
            ryml::Tree atree;
            try
            {
                atree = parse_json_safe(args_json);
                aroot = atree.rootref();
            }
            catch (const std::exception& e)
            {
                return std::string("{\"error\":") + json_string(std::string("invalid arguments: ") + e.what()) + "}";
            }

            if (!aroot.has_child("component") || !aroot.has_child("field"))
                return "{\"error\":\"missing 'entity', 'component' or 'field' argument\"}";

            uint32_t raw_id = 0;
            if (!aroot.has_child("entity") || !read_uint32(aroot["entity"], &raw_id))
                return "{\"error\":\"missing or malformed 'entity' argument\"}";
            auto eid = static_cast<entt::entity>(raw_id);

            std::string component_name, field_name;
            aroot["component"] >> component_name;
            aroot["field"] >> field_name;

            auto& reg = engine::instance().default_scene().registry();

            std::string error;
            entt::meta_any comp_ref = find_component_ref(reg, eid, component_name, &error);
            if (!comp_ref)
                return std::string("{\"error\":") + json_string(error) + "}";

            entt::meta_data d = find_field(comp_ref, field_name);
            if (!d)
                return "{\"error\":\"no such field on component\"}";

            auto member = d.get(comp_ref);
            std::string value_json = field_to_json(member);
            if (value_json.empty())
                return "{\"error\":\"unsupported field type\"}";

            return "{\"value\":" + value_json + "}";
        });

    register_tool("entity_field_set",
        "Writes a single top-level field of a component on an entity in the default scene. "
        "Argument: {\"entity\": <integer entity id>, \"component\": <string>, \"field\": <string>, "
        "\"value\": <new value>}. Nested/struct fields are not supported (v1 is flat-only). "
        "Resource-pointer fields accept a JSON string (VFS resource path) or integer (resource "
        "hash id) to set, or null to clear.",
        "{\"type\":\"object\",\"properties\":{"
        "\"entity\":{\"type\":\"integer\"},"
        "\"component\":{\"type\":\"string\"},"
        "\"field\":{\"type\":\"string\"},"
        // "value"'s actual shape depends on the RTTI-resolved field type,
        // which is only known once entity/component/field are resolved at
        // call time — so it's deliberately left untyped here (any JSON
        // value is accepted).
        "\"value\":{}"
        "},\"required\":[\"entity\",\"component\",\"field\",\"value\"]}",
        [](const std::string& args_json) -> std::string {
            ryml::ConstNodeRef aroot;
            ryml::Tree atree;
            try
            {
                atree = parse_json_safe(args_json);
                aroot = atree.rootref();
            }
            catch (const std::exception& e)
            {
                return std::string("{\"error\":") + json_string(std::string("invalid arguments: ") + e.what()) + "}";
            }

            if (!aroot.has_child("component") || !aroot.has_child("field") || !aroot.has_child("value"))
                return "{\"error\":\"missing 'entity', 'component', 'field' or 'value' argument\"}";

            uint32_t raw_id = 0;
            if (!aroot.has_child("entity") || !read_uint32(aroot["entity"], &raw_id))
                return "{\"error\":\"missing or malformed 'entity' argument\"}";
            auto eid = static_cast<entt::entity>(raw_id);

            std::string component_name, field_name;
            aroot["component"] >> component_name;
            aroot["field"] >> field_name;

            auto& reg = engine::instance().default_scene().registry();

            std::string error;
            entt::meta_any comp_ref = find_component_ref(reg, eid, component_name, &error);
            if (!comp_ref)
                return std::string("{\"error\":") + json_string(error) + "}";

            entt::meta_data d = find_field(comp_ref, field_name);
            if (!d)
                return "{\"error\":\"no such field on component\"}";

            if (!json_to_field(d, comp_ref, aroot["value"], &error))
                return std::string("{\"error\":") + json_string(error) + "}";

            return "{\"ok\":true}";
        });

    register_tool("sys_function_list",
        "Lists RTTI-registered functions callable via sys_function_call, with their arity. "
        "Argument: {\"system\": <optional string; omitted/empty = all systems>}.",
        "{\"type\":\"object\",\"properties\":{\"system\":{\"type\":\"string\"}}}",
        [](const std::string& args_json) -> std::string {
            ryml::ConstNodeRef aroot;
            ryml::Tree atree;
            try
            {
                atree = parse_json_safe(args_json);
                aroot = atree.rootref();
            }
            catch (const std::exception& e)
            {
                return std::string("{\"error\":") + json_string(std::string("invalid arguments: ") + e.what()) + "}";
            }

            std::string filter_system;
            if (aroot.has_child("system"))
                aroot["system"] >> filter_system;

            ryml::Tree out;
            ryml::NodeRef oroot = out.rootref();
            oroot |= ryml::MAP;
            auto seq = oroot.append_child();
            seq << ryml::key("functions");
            seq |= ryml::SEQ;

            auto system_t = entt::resolve<nb::system>();
            for (auto&& [cpp_id, type] : entt::resolve())
            {
                if (!type.can_cast(system_t))
                    continue;
                const rtti::type_info* info = type.custom();
                if (!info || info->type_class != rtti::TYPE_CLASS_SYSTEM)
                    continue;
                if (!filter_system.empty() && filter_system != info->identifier.c_str())
                    continue;

                for (auto&& [fhash, f] : type.func())
                {
                    const rtti::func_info* fi = f.custom();
                    if (!fi)
                        continue;

                    auto item = seq.append_child();
                    item |= ryml::MAP;
                    item.append_child() << ryml::key("system") << info->identifier.c_str();
                    item.append_child() << ryml::key("name") << fi->identifier.c_str();
                    item.append_child() << ryml::key("arity") << (uint32_t)f.arity();
                }
            }

            return ryml::emitrs_json<std::string>(out);
        });

    register_tool("sys_function_call",
        "Invokes an RTTI-registered function on a currently-running system. "
        "Argument: {\"system\": <string>, \"function\": <string>, \"args\": <optional array of "
        "positional argument values, default []>}. Use sys_function_list to discover available "
        "system/function pairs and their arity. Argument/return value coverage matches "
        "entity_field_get/entity_field_set (bool/float/int/unsigned int/entt::entity/std::string/"
        "glm vec2-4/quat/resource_ptr); a void return is reported as a null result.",
        "{\"type\":\"object\",\"properties\":{"
        "\"system\":{\"type\":\"string\"},"
        "\"function\":{\"type\":\"string\"},"
        "\"args\":{\"type\":\"array\"}"
        "},\"required\":[\"system\",\"function\"]}",
        [](const std::string& args_json) -> std::string {
            ryml::ConstNodeRef aroot;
            ryml::Tree atree;
            try
            {
                atree = parse_json_safe(args_json);
                aroot = atree.rootref();
            }
            catch (const std::exception& e)
            {
                return std::string("{\"error\":") + json_string(std::string("invalid arguments: ") + e.what()) + "}";
            }

            if (!aroot.has_child("system") || !aroot.has_child("function"))
                return "{\"error\":\"missing 'system' or 'function' argument\"}";

            std::string system_name, func_name;
            aroot["system"] >> system_name;
            aroot["function"] >> func_name;

            std::string error;
            void* instance = nullptr;
            entt::meta_type type = find_system_type(system_name, &instance, &error);
            if (!type)
                return std::string("{\"error\":") + json_string(error) + "}";

            entt::meta_func func = find_func(type, func_name);
            if (!func)
                return "{\"error\":\"no such function on system\"}";

            std::vector<ryml::ConstNodeRef> arg_nodes;
            if (aroot.has_child("args"))
            {
                auto args_node = aroot["args"];
                if (!args_node.is_seq())
                    return "{\"error\":\"'args' must be an array\"}";
                for (auto child : args_node.children())
                    arg_nodes.push_back(child);
            }

            if (arg_nodes.size() != func.arity())
                return std::string("{\"error\":") + json_string(
                    "expected " + std::to_string(func.arity()) + " argument(s), got " +
                    std::to_string(arg_nodes.size())) + "}";

            std::vector<entt::meta_any> args;
            args.reserve(arg_nodes.size());
            for (size_t i = 0; i < arg_nodes.size(); ++i)
            {
                entt::meta_any placeholder = default_of_type(func.arg((entt::id_type)i));
                if (!placeholder)
                    return std::string("{\"error\":") + json_string(
                        "unsupported argument type at position " + std::to_string(i)) + "}";
                if (!assign_json_to_meta_any(placeholder, arg_nodes[i], &error))
                    return std::string("{\"error\":") + json_string(
                        "argument " + std::to_string(i) + ": " + error) + "}";
                args.push_back(std::move(placeholder));
            }

            // entt::meta_handle has constructors from meta_any& (mutable)
            // and const meta_any& (immutable) but none from meta_any&& — so
            // passing the from_void() temporary directly into invoke()
            // silently binds through the CONST overload, producing an
            // immutable handle. entt's internal dispatch requires
            // instance->try_cast<Type>() (non-const) for non-const member
            // functions, which fails on an immutable handle — while a const
            // member function only needs try_cast<const Type>(), which
            // still succeeds. That exactly matched what looked like "void
            // functions always report failure": get_time_scale (const) kept
            // working while set_time_scale/bgm_gain (non-const) always
            // failed. Fix: bind to a named lvalue first so the mutable
            // overload is selected.
            entt::meta_any instance_any = type.from_void(instance);
            entt::meta_any result = func.invoke(instance_any, args.empty() ? nullptr : args.data(), args.size());
            if (!result)
                return "{\"error\":\"function invocation failed\"}";

            std::string value_json = field_to_json(result);
            if (value_json.empty())
                return "{\"ok\":true,\"result\":null}";

            return "{\"ok\":true,\"result\":" + value_json + "}";
        });

    register_tool("eval_lua",
        "Executes arbitrary Lua code in the running engine's script_lua system (the same "
        "lua_State used for scripted entities/systems — full access to whatever the game's Lua "
        "bindings expose, including live component/system access). Argument: {\"code\": <string>}. "
        "Returns the stringified first return value of the chunk (empty if none). SAFETY: this is "
        "the least restricted tool available — it can read and mutate arbitrary engine/game state "
        "and is not sandboxed; use deliberately.",
        "{\"type\":\"object\",\"properties\":{\"code\":{\"type\":\"string\"}},\"required\":[\"code\"]}",
        [this](const std::string& args_json) -> std::string {
            ryml::ConstNodeRef aroot;
            ryml::Tree atree;
            try
            {
                atree = parse_json_safe(args_json);
                aroot = atree.rootref();
            }
            catch (const std::exception& e)
            {
                return std::string("{\"error\":") + json_string(std::string("invalid arguments: ") + e.what()) + "}";
            }

            if (!aroot.has_child("code"))
                return "{\"error\":\"missing 'code' argument\"}";

            std::string code;
            aroot["code"] >> code;

            // Delegate to sys_function_call's own tool function for the
            // actual dispatch, rather than re-deriving the same
            // entt::meta_func::invoke() call by hand — guarantees identical
            // (already independently verified) behavior instead of risking
            // subtle divergence, and avoids duplicating the invocation logic.
            auto it = _tools.find("sys_function_call");
            if (it == _tools.end())
                return "{\"error\":\"internal: sys_function_call tool missing\"}";

            std::string inner_args = "{\"system\":\"script_lua\",\"function\":\"eval\",\"args\":[" +
                                      json_string(code) + "]}";
            return it->second.fn(inner_args);
        });
}

std::string mcp::_dispatch_rpc(const std::string& request_json)
{
    ryml::Tree tree;
    ryml::ConstNodeRef root;
    try
    {
        tree = parse_json_safe(request_json);
        root = tree.rootref();
    }
    catch (const std::exception& e)
    {
        log::warn("[mcp] request parse error: %s", e.what());
        return rpc_error("null", -32700, std::string("parse error: ") + e.what());
    }

    std::string id_json = root.has_child("id") ? node_to_json(root["id"]) : "null";

    if (!root.has_child("method"))
    {
        log::warn("[mcp] request missing 'method'");
        return rpc_error(id_json, -32600, "missing method");
    }

    std::string method;
    root["method"] >> method;

    if (method == "initialize")
        return _handle_initialize(id_json);
    if (method == "notifications/initialized")
        return std::string(); // notification, no response
    if (method == "tools/list")
        return _handle_tools_list(id_json);
    if (method == "tools/call")
    {
        std::string params_json = root.has_child("params") ? node_to_json(root["params"]) : "{}";
        return _handle_tools_call(id_json, params_json);
    }

    log::warn("[mcp] unknown method: %s", method.c_str());
    return rpc_error(id_json, -32601, "method not found: " + method);
}

std::string mcp::_handle_initialize(const std::string& id_json)
{
    // NOTE: pin/verify this against whatever MCP protocol revision the target
    // clients speak — this is a placeholder pending real client testing.
    std::string result =
        "{\"protocolVersion\":\"2025-06-18\","
        "\"capabilities\":{\"tools\":{}},"
        "\"serverInfo\":{\"name\":\"newbase-mcp\",\"version\":\"0.1.0\"}}";
    return rpc_result(id_json, result);
}

std::string mcp::_handle_tools_list(const std::string& id_json)
{
    // Hand-assembled (like rpc_result/rpc_error) rather than built via a
    // ryml::Tree, since each tool's input_schema_json is already a raw JSON
    // fragment — splicing it into a separately-built tree would mean mixing
    // arenas across trees, which we've deliberately avoided elsewhere in
    // this file (see node_to_json's comment).
    std::string tools_json = "[";
    bool first = true;
    for (auto& [name, entry] : _tools)
    {
        if (!first)
            tools_json += ",";
        first = false;
        tools_json += "{\"name\":" + json_string(name) +
                      ",\"description\":" + json_string(entry.description) +
                      ",\"inputSchema\":" + entry.input_schema_json + "}";
    }
    tools_json += "]";

    std::string result = "{\"tools\":" + tools_json + "}";
    return rpc_result(id_json, result);
}

std::string mcp::_handle_tools_call(const std::string& id_json, const std::string& params_json)
{
    ryml::Tree ptree;
    ryml::ConstNodeRef proot;
    try
    {
        ptree = parse_json_safe(params_json);
        proot = ptree.rootref();
    }
    catch (const std::exception& e)
    {
        log::warn("[mcp] tools/call invalid params: %s", e.what());
        return rpc_error(id_json, -32602, std::string("invalid params: ") + e.what());
    }

    if (!proot.has_child("name"))
    {
        log::warn("[mcp] tools/call missing tool name");
        return rpc_error(id_json, -32602, "missing tool name");
    }

    std::string tool_name;
    proot["name"] >> tool_name;

    if (_tools.find(tool_name) == _tools.end())
    {
        log::warn("[mcp] tools/call unknown tool: %s", tool_name.c_str());
        return rpc_error(id_json, -32602, "unknown tool: " + tool_name);
    }

    std::string args_json = proot.has_child("arguments") ? node_to_json(proot["arguments"]) : "{}";

    auto call = std::make_shared<pending_call>();
    call->tool_name = tool_name;
    call->args_json = args_json;
    std::future<std::string> fut = call->result.get_future();

    {
        std::lock_guard<std::mutex> lock(_pending_mtx);
        _pending.push_back(call);
    }

    // Bounded wait — if the main loop stalls, fail loudly rather than hang the
    // HTTP worker thread (and the caller) forever.
    if (fut.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
    {
        log::warn("[mcp] tool '%s' timed out waiting for the main thread", tool_name.c_str());
        return rpc_error(id_json, -32000, "tool call timed out waiting for the main thread");
    }

    std::string tool_result = fut.get();
    std::string content =
        "{\"content\":[{\"type\":\"text\",\"text\":" + json_string(tool_result) + "}]}";
    return rpc_result(id_json, content);
}

// RTTI metadata
extern "C" void _rtti_init_mcp()
{
    entt::meta_factory<nb::mcp>{}
        .type("mcp"_hs)
        .custom<rtti::type_info>(rtti::type_info{.identifier="mcp", .type_class=rtti::TYPE_CLASS_SYSTEM})
        .base<nb::system>();
    entt::meta_factory<std::shared_ptr<nb::mcp>>{rtti::ctx_systems()}
        .type("mcp_shared"_hs)
        .ctor<&rtti::shared_ptr_builder<nb::mcp>>()
        .conv<std::shared_ptr<nb::system>>();
}
