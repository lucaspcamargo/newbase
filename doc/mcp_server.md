# MCP Server

newbase can expose a running game/engine instance to an AI agent (or any
[MCP](https://modelcontextprotocol.io/) client) over HTTP, for live
introspection and control: listing entities and their components, reading and
writing component fields, calling system functions, and running arbitrary
Lua. This is primarily meant as a development/debugging aid — driving the
engine from an editor-like agent, poking at live state, or scripting quick
experiments without recompiling.

## Enabling it

The server is compiled in only when the CMake option
`NEWBASE_ENABLE_MCP` is `ON` (default `OFF`). It is not supported on
Emscripten builds.

```
cmake -DNEWBASE_ENABLE_MCP=ON ...
```

When enabled, add an `mcp` entry under `systems:` in the engine's config YAML
to start the server:

```yaml
systems:
  mcp:
    port: 8765
    bind_host: 127.0.0.1
```

If omitted, `port` defaults to 8765 and `bind_host` defaults to
`127.0.0.1` — the server only accepts connections from the local machine.

Set `bind_host` to `0.0.0.0` (all interfaces) or a specific IP to allow
connections from other machines, e.g. to drive the engine from an agent
running on a different host on the same network. The server has **no
authentication** — anything that can reach the port has full read/write
access to engine state, including arbitrary Lua execution via `eval_lua`.
Only bind beyond `127.0.0.1` on a trusted network, and the engine logs a
warning at startup whenever `bind_host` is not the loopback address.

## Connecting a client

The server speaks MCP over Streamable HTTP, JSON-RPC 2.0, POSTed to `/`.
Point any MCP-compatible client at it, e.g. via an `.mcp.json`:

```json
{
  "mcpServers": {
    "newbase-demo": {
      "type": "http",
      "url": "http://127.0.0.1:8765/"
    }
  }
}
```

The server must already be listening (i.e. the game process must be running
with the `mcp` system configured) before the client connects. If the game
process is restarted, reconnect the client.

## How calls reach engine state

The HTTP server runs on its own worker thread(s) and never touches engine
state directly. A `tools/call` request is queued and executed on the main
thread during the `POST_UPDATE` step phase, then the result is handed back
to the waiting HTTP request. This means:

- Tool calls only take effect (and only see up-to-date state) once per
  frame, not instantly.
- A call blocks its HTTP request for up to 5 seconds waiting for the main
  loop; if the engine is stalled, the call fails with a timeout error
  rather than hanging forever.
- Every tool invocation is logged (`[mcp] tool call: ...`) on the engine's
  console, along with a warning if it fails to parse or returns an error —
  worth checking there when a tool call behaves unexpectedly.

## Tools

All arguments and results are JSON. Field/argument type coverage is the same
throughout the tool set: `bool`, `float`, `int`, `unsigned int`,
`entt::entity` (as an integer), `std::string`, `glm::vec2`/`vec3`/`vec4`,
`glm::quat`, and resource pointers. Anything else (nested structs, containers,
etc.) is not currently supported and calls touching such a field will return
an error.

### `ping`

Health check. No arguments. Replies `{"message":"pong"}` — useful to confirm
the bridge and the main-thread round trip are alive.

### `sys_list`

Lists all RTTI-registered systems and whether each is currently
instantiated/running. No arguments.

### `sys_function_list`

Lists RTTI-registered functions callable via `sys_function_call`, with their
arity.

- `system` (optional string) — restrict to one system's functions; omitted
  or empty lists all systems.

Only functions explicitly registered in RTTI (`rtti::func_info`) show up
here — most system member functions are not exposed this way unless
deliberately opted in.

### `sys_function_call`

Invokes an RTTI-registered function on a currently-running system.

- `system` (string, required)
- `function` (string, required)
- `args` (array, optional, default `[]`) — positional argument values.

Use `sys_function_list` first to discover the system/function pairs and
their arity — the call fails if the argument count doesn't match. A `void`
return is reported as `{"ok":true,"result":null}`.

### `entity_list`

Lists all entities in a scene, with their name (if any) and parent entity id
(if any).

- `scene_id` (optional string) — omitted/empty means the default scene.
  Only the default scene exists today; this argument is accepted for
  forward compatibility.

### `entity_find`

Finds the first entity in the default scene whose `cstructure` name matches
exactly.

- `name` (string, required)

Returns `{"entity": <id>}`, or `{"entity": null}` if no entity has that name.

### `entity_create`

Creates a new entity in the default scene.

- `name` (optional string)
- `parent` (optional integer entity id)

A `cstructure` component is only attached if a name and/or parent is given —
a bare `entity_create` with no arguments creates a component-less entity.
Returns `{"entity": <new entity id>}`.

### `entity_destroy`

Queues an entity in the default scene for destruction.

- `entity` (integer, required)

MCP tool calls execute during the `POST_UPDATE` step phase (see [How calls
reach engine state](#how-calls-reach-engine-state)), which comes after
`PREPARE` in the frame — so the entity is actually destroyed at the end of
the *next* `PREPARE` phase, not immediately. A subsequent
`entity_list`/`entity_dump` call in the same tool batch may still see it.

### `entity_component_list`

Lists the RTTI-registered components attached to one entity in the default
scene.

- `entity` (integer, required)

### `entity_field_get`

Reads a single top-level field of a component on an entity in the default
scene.

- `entity` (integer, required)
- `component` (string, required) — the component's RTTI identifier (e.g.
  `"spatial"`, `"sprite"`), not its C++ type name.
- `field` (string, required) — the field's RTTI identifier.

Nested/struct fields are **not** supported — only flat, top-level fields.
Resource-pointer fields resolve to their VFS path (if the resource manager
knows it) or their raw hash id otherwise, or `null` if unset.

### `entity_field_set`

Writes a single top-level field of a component on an entity in the default
scene.

- `entity` (integer, required)
- `component` (string, required)
- `field` (string, required)
- `value` (required, any JSON shape — depends on the field's actual type,
  resolved server-side) — e.g. `{"x": 1, "y": 2}` for a `glm::vec2` field, a
  plain number/bool/string for scalar fields.

Resource-pointer fields accept either a JSON string (a VFS resource path) or
a JSON integer (a raw resource hash id) to set the pointer, or `null` to
clear it.

### `entity_dump`

Full dump of an entity in the default scene: name, parent, every RTTI
component attached, and every one of its supported top-level fields (same
coverage as `entity_field_get`; unsupported fields, e.g. nested structs, are
silently omitted rather than erroring).

- `entity` (integer, required)
- `children` (optional bool, default `false`) — recursively dump child
  entities too (via the `cstructure` parent/child tree), nested under a
  `"children"` array in the result.

### `rtti_dump`

Enumerates RTTI-registered types (components, resources, systems, etc.),
each with its fields and functions.

- `type_class` (optional string) — one of `"component"`, `"resource"`,
  `"system"`, `"singleton"`, `"res_storage"`, `"service"`,
  `"resource_ptr"`; restricts the listing to that class.
- `name` (optional string) — exact RTTI identifier; restricts to a single
  type.

Only fields/functions that carry `rtti::data_info`/`rtti::func_info` custom
metadata are listed — plain unregistered `entt::meta` members won't appear.
Useful to discover a component's field names (for `entity_field_get`/`_set`)
or a system's function names (for `sys_function_call`) without reading the
C++ source.

### `eval_lua`

Executes arbitrary Lua code in the running engine's `script_lua` system —
the same `lua_State` used for scripted entities/systems, so it has full
access to whatever the game's Lua bindings expose, including live
component/system access.

- `code` (string, required)

Returns the stringified first return value of the chunk (empty if the chunk
returns nothing).

**This is the least restricted tool available.** It can read and mutate
arbitrary engine/game state and is not sandboxed in any way — it runs with
the same privileges as the game's own scripts. Use deliberately, and only
against a trusted local engine instance (which is the only thing the server
binds to in the first place, since it only listens on `127.0.0.1`).

## Naming convention

Tools are prefixed by scope: `sys_*` for system-scoped tools, `entity_*` for
entity-scoped tools, and no prefix when there's no natural scope (`ping`,
`eval_lua`, `rtti_dump`). Keep this convention when adding new tools.
