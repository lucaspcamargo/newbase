#pragma once

#include <newbase/system.hpp>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace httplib { class Server; }

namespace nb {

// Embedded MCP (Model Context Protocol) server. Exposes engine introspection
// and control as MCP tools over Streamable HTTP, so an external AI agent can
// attach to a running instance.
//
// Threading: httplib runs its own worker thread pool. A "tools/call" request
// arriving on an httplib worker thread is never executed there directly —
// it's queued and handed to the main thread at a defined step_phase, since
// entt::registry/engine state is not safe to touch off the main thread.
class mcp : public system
{
public:
    mcp();
    ~mcp();

    SDL_InitFlags sdl_subsystems(ryml::ConstNodeRef cfg) override { return 0; }
    entt::id_type metatype_id() override { return entt::hashed_string{"mcp"}.value(); }

    bool init(ryml::ConstNodeRef cfg) override;
    bool step(step_phase) override;
    bool event(SDL_Event*) override;

    // A registered tool takes the raw JSON "arguments" object (as text) and
    // returns raw JSON to place in the MCP result's text content. Called on
    // the main thread only — safe to touch engine/registry state.
    //
    // input_schema_json is a raw JSON Schema object (e.g.
    // {"type":"object","properties":{"entity":{"type":"integer"}},
    // "required":["entity"]}), advertised verbatim in tools/list. Most tool
    // arguments here have a statically-known shape (entity ids are always
    // integers, component/field names are always strings) even though the
    // underlying engine state is dynamically typed via RTTI — only a true
    // per-call-dynamic value (e.g. set_field's "value", whose type depends
    // on which RTTI field is being written) needs a loose/any-type schema.
    // Declaring real types where we can helps MCP clients send correctly
    // shaped arguments instead of guessing (observed: clients JSON-stringify
    // arguments whose type isn't declared).
    using tool_fn = std::function<std::string(const std::string& args_json)>;
    void register_tool(std::string name, std::string description, std::string input_schema_json, tool_fn fn);

private:
    struct tool_entry
    {
        std::string description;
        std::string input_schema_json;
        tool_fn     fn;
    };

    struct pending_call
    {
        std::string                       tool_name;
        std::string                       args_json;
        std::promise<std::string>         result; // JSON text, or an error marker
    };

    void _register_builtin_tools();
    std::string _dispatch_rpc(const std::string& request_json);
    std::string _handle_tools_call(const std::string& id_json, const std::string& params_json);
    std::string _handle_tools_list(const std::string& id_json);
    std::string _handle_initialize(const std::string& id_json);

    std::unique_ptr<httplib::Server> _svr;
    std::thread                      _svr_thread;
    int                               _port {8765};

    std::mutex                       _pending_mtx;
    std::deque<std::shared_ptr<pending_call>> _pending;

    std::map<std::string, tool_entry> _tools;
};

}
