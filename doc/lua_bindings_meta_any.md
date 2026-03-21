# Lua Bindings via `entt::meta_any`

Design notes for the `script_lua` binding layer, using `entt::meta` as the RTTI backbone.

## The Box Pattern

A single "box" struct is stored as Lua userdata, decoupled from any specific type:

```cpp
struct lua_nb_box {
    entt::meta_any value;          // the actual data (owning or non-owning)
    std::shared_ptr<void> owner;   // optional: keeps an external object alive
};
```

Allocated with placement new inside `lua_newuserdata`, destroyed in `__gc` by calling the destructor explicitly. All boxes share **one** metatable; `__index` dispatches through `entt::meta` at runtime using `value.type()`.

---

## The Three Ownership Cases

**1. Owned value** (e.g., a `vec2` constructed in Lua):
```cpp
box->value = entt::meta_any{ glm::vec2{1, 2} };  // meta_any owns it
box->owner = nullptr;
```
`__gc` calls `box->~lua_nb_box()` — `meta_any` destructor handles cleanup.

**2. Non-owning reference** (e.g., a component from the registry):
```cpp
cspatial *comp = reg.try_get<cspatial>(entity);
box->value = entt::forward_as_meta(*comp);  // non-owning ref
box->owner = nullptr;  // registry keeps it alive
```
`meta_any` from `forward_as_meta` is non-owning — `__gc` is a no-op for the pointed-to data. Care is needed: if the entity is destroyed while Lua holds the box, the reference dangles. A validity strategy is required (entity version check, or a weak pointer to the registry).

**3. Shared pointer** (systems):
```cpp
auto sys = engine::instance().system_ptr<nb::audio>(); // shared_ptr<audio>
box->value = entt::forward_as_meta(*sys);  // ref to *audio for method dispatch
box->owner = sys;                          // shared_ptr<void> keeps it alive
```
The key insight: separate **lifetime** (the `shared_ptr`) from **dispatch** (the `meta_any` ref to the raw object). Method calls go through the `audio` meta type directly — no need to register functions on `shared_ptr<audio>`.

---

## Method Dispatch in `__index`

```cpp
int lua_nb_box_index(lua_State *L) {
    auto *box = static_cast<lua_nb_box*>(lua_touserdata(L, 1));
    const char *key = lua_tostring(L, 2);
    auto hash = entt::hashed_string{key}.value();
    auto type = box->value.type();

    // data member → get and push new box
    if (auto d = type.data(hash); d) {
        auto result = d.get(box->value);
        push_meta_any(L, std::move(result));
        return 1;
    }

    // function → push closure with box pointer + hash as upvalues
    if (auto f = type.func(hash); f) {
        lua_pushlightuserdata(L, box);
        lua_pushinteger(L, (lua_Integer)hash);
        lua_pushcclosure(L, lua_nb_func_call, 2);
        return 1;
    }

    lua_pushnil(L);
    return 1;
}
```

For `lua_nb_func_call`, pull `box` and `hash` from upvalues, collect arguments (converting each from Lua → `meta_any`), then invoke:

```cpp
auto type = box->value.type();
auto func = type.func(hash);
// build args[] from Lua stack...
auto result = func.invoke(box->value, args, argc);
if (result) push_meta_any(L, std::move(result));
```

---

## Converting Lua → `meta_any`

When Lua calls a C++ function with arguments, each Lua value must be converted to a `meta_any` of the right type:

- **Primitive types** (number, string, bool): straightforward — `entt::meta_any{lua_tonumber(...)}`, etc.
- **Userdata boxes**: the box already contains a `meta_any` — pass it as a ref: `box->value.as_ref()`
- **Type coercion**: `func.arg(i)` gives the expected `entt::meta_type` for argument `i`, so you can try to cast the incoming `meta_any` with `allow_cast()`.

The entt meta invoke path attempts coercion, but only for registered conversions. Important ones to register explicitly: `float` ↔ `double`, `shared_ptr<T>` → `T*`.

---

## Relation to Existing Registration

Systems are already registered with `.conv<std::shared_ptr<nb::system>>()` for the factory pattern (see `_rtti_init_audio` etc.). The same mechanism can register `shared_ptr<T>` → `T*` conversions if needed elsewhere. `entt::forward_as_meta` is the right primitive for the reference case and is equivalent to `meta_any::as_ref()` on an existing `meta_any`.

The main remaining work is:
- The `lua_nb_box` struct + metatable plumbing (`__index`, `__newindex`, `__gc`, `__tostring`)
- The Lua ↔ `meta_any` conversion layer (both directions)
- A `push_meta_any` helper that selects the right ownership mode
