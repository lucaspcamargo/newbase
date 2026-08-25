#include "lupi_internal.hpp"
#include <newbase/res/manager.hpp>
#include <algorithm>
#include <new>
#include <string>
#include <vector>

using namespace nb;

namespace {

struct lupi_file {
    std::string contents;
    size_t position { 0 };
    bool closed { false };
};

std::string resolve_cart_path(const lupi_p& p, const char* name)
{
    if (!name || name[0] == '/') return {};

    std::vector<std::string> parts;
    std::string component;
    for (const char* cursor = name;; ++cursor) {
        if (*cursor == '/' || *cursor == '\\' || *cursor == '\0') {
            if (component == "..") {
                if (parts.empty()) return {};
                parts.pop_back();
            } else if (!component.empty() && component != ".") {
                parts.push_back(component);
            }
            component.clear();
            if (*cursor == '\0') break;
        } else {
            component += *cursor;
        }
    }

    std::string path = p.cart_dir;
    for (const auto& part : parts) {
        path += part;
        path += '/';
    }
    if (!path.empty()) path.pop_back();
    return path;
}

lupi_file* check_file(lua_State* L, int index)
{
    auto* file = static_cast<lupi_file*>(luaL_checkudata(L, index, "lupi.file"));
    if (file->closed) luaL_error(L, "attempt to use a closed file");
    return file;
}

int file_close(lua_State* L)
{
    auto* file = static_cast<lupi_file*>(luaL_checkudata(L, 1, "lupi.file"));
    file->closed = true;
    file->contents.clear();
    file->position = 0;
    return 0;
}

int file_gc(lua_State* L)
{
    auto* file = static_cast<lupi_file*>(luaL_checkudata(L, 1, "lupi.file"));
    file->~lupi_file();
    return 0;
}

int file_read(lua_State* L)
{
    lupi_file* file = check_file(L, 1);
    if (lua_isnoneornil(L, 2)) {
        luaL_argerror(L, 2, "read format is required");
    }

    if (lua_type(L, 2) == LUA_TNUMBER) {
        lua_Integer requested = luaL_checkinteger(L, 2);
        if (requested < 0) luaL_argerror(L, 2, "byte count must not be negative");
        size_t count = static_cast<size_t>(requested);
        count = std::min(count, file->contents.size() - file->position);
        lua_pushlstring(L, file->contents.data() + file->position, count);
        file->position += count;
        return 1;
    }

    const char* format = luaL_checkstring(L, 2);
    if (std::string(format) == "*a" || std::string(format) == "*A") {
        lua_pushlstring(L, file->contents.data() + file->position,
                        file->contents.size() - file->position);
        file->position = file->contents.size();
        return 1;
    }
    if (std::string(format) == "*l" || std::string(format) == "*L") {
        if (file->position >= file->contents.size()) {
            lua_pushnil(L);
            return 1;
        }
        size_t end = file->contents.find('\n', file->position);
        if (end == std::string::npos) end = file->contents.size();
        size_t length = end - file->position;
        if (length > 0 && file->contents[file->position + length - 1] == '\r') --length;
        size_t returned_length = format[1] == 'L' && end < file->contents.size()
                       ? end - file->position + 1
                       : length;
        lua_pushlstring(L, file->contents.data() + file->position, returned_length);
        file->position = end < file->contents.size() ? end + 1 : end;
        return 1;
    }

    return luaL_error(L, "unsupported read format '%s'", format);
}

int io_open(lua_State* L)
{
    auto* p = static_cast<lupi_p*>(lua_touserdata(L, lua_upvalueindex(1)));
    const char* name = luaL_checkstring(L, 1);
    const char* mode = luaL_optstring(L, 2, "r");
    if (std::string(mode) != "r" && std::string(mode) != "rb") {
        lua_pushnil(L);
        lua_pushliteral(L, "Lupi files are read-only");
        return 2;
    }

    std::string path = resolve_cart_path(*p, name);
    std::vector<char> bytes;
    if (path.empty() || !rman().read_all_sync(entt::hashed_string{path.c_str()}.value(), bytes, false)) {
        lua_pushnil(L);
        lua_pushfstring(L, "cannot open '%s'", name);
        return 2;
    }

    auto* file = static_cast<lupi_file*>(lua_newuserdatauv(L, sizeof(lupi_file), 0));
    new (file) lupi_file{std::string(bytes.begin(), bytes.end())};
    luaL_setmetatable(L, "lupi.file");
    return 1;
}

}

void nb::lupi_register_io(lua_State* L, lupi_p& p)
{
    if (luaL_newmetatable(L, "lupi.file")) {
        lua_pushcfunction(L, file_gc);
        lua_setfield(L, -2, "__gc");
        lua_newtable(L);
        lua_pushcfunction(L, file_read);
        lua_setfield(L, -2, "read");
        lua_pushcfunction(L, file_close);
        lua_setfield(L, -2, "close");
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);

    lua_getglobal(L, "io");
    lua_pushlightuserdata(L, &p);
    lua_pushcclosure(L, io_open, 1);
    lua_setfield(L, -2, "open");
    lua_pop(L, 1);
}