#pragma once

#include <memory>
#include <cstring>

namespace nb {
namespace rtti {

// there is an issue when using const char * (or pointers in general) for custom rtti data
// this simple struct helps us bypass it for static strings
// discussion at https://github.com/skypjack/entt/discussions/962#discussioncomment-12589963
struct cstr {
    const char * _val;
    
    cstr (const char * strptr) : _val (strptr) {}
    
    operator const char *() const 
    { 
        return _val;
    }
    bool operator == (const char *other) const
    {
        return !std::strcmp(other, _val);
    }
};

// this is basically same as above, but has a known fixed size and owns the data
template<size_t N>
struct cstrn {
    char _val[N];

    cstrn () { _val[0] = '\0'; }

    cstrn (const char * strptr) {
        std::strncpy(_val, strptr, N);
        _val[N-1] = 0;
    }
    
    operator const char *() const 
    { 
        return _val;
    }
    bool operator == (const char *other) const
    {
        return !strcmp(other, _val);
    }
};


struct system_info
{
    cstrn<32> identifier;
};

struct component_type_info
{
    cstrn<32> identifier;
    bool can_add;
};

struct resource_type_info
{
    cstrn<32> identifier;
};

struct res_storage_type_info
{
    cstrn<32> identifier;

    bool sure_writable;
    bool maybe_writable;

    // whether the storage type surely supports, or may possibly support, scanning it for files and sizes
    bool sure_scaneable;
    bool maybe_scaneable;
};

struct func_info
{
    cstrn<32> identifier;
    // incredibly, entt::meta provides all the rest for us
};

// this is used to define builder functions for non-copyable and/or non-movable types,
// since they cannot be properly stored in an entt::any...
// was going to use unique_ptr but since it is not copy-constructible, this seems to be an issue
// with entt::any for the same reasons...
template <typename T, typename... Args>
std::shared_ptr<T> shared_ptr_builder(Args... args)
{
    return std::make_shared<T>(args...);
}

}
}