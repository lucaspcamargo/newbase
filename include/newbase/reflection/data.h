#pragma once

#include <memory>
#include <cstring>
#include <entt/entt.hpp>

namespace nb {
namespace rtti {

// there is an issue when using const char * (or pointers in general) for custom rtti data
// this simple struct helps us bypass it for static strings
// discussion at https://github.com/skypjack/entt/discussions/962#discussioncomment-12589963
struct cstr {
    const char * _val;
    
    cstr (const char * strptr) : _val (strptr) {}

    const char * c_str() const 
    {
        return _val;
    }
    
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

    const char * c_str() const 
    {
        return _val;
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

struct singleton_info
{
    cstrn<32> identifier;
};

struct component_type_info
{
    // making a lua binding of a component returns the identifier to use for the local getter,
    // and a function that can be used to resolve the reference in a state, given the id and registry
    // we cannot just pass the component pointer to the function. That is faster but components do not
    // have pointer stability
    // maybe we can do something better in the future (e.g. update references when calling into lua again)
    // for sure components that /do have/ pointer stability can bind more simply
    using bind_result = std::pair<const char *, std::function<void(void*, entt::entity, entt::registry&)>>; 

    cstrn<32> identifier;
    bool can_add;
    std::function<bind_result(void*)> _bind_func;
    const char *editor_icon;

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

struct data_info
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