#pragma once

#include <memory>

namespace nb {
namespace rtti {

// there is an issue when using const char * (or pointers in general) for custom rtti data
// this simple struct helps us bypass it for now
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
        return !strcmp(other, _val);
    }
};

// this is used to define builder functions for non-copyable and/or non-movable types,
// since they cannot be properly stored in an entt::any
// was going to use unique_ptr but since it is not copy-constructible, this may be an issue
// with entt::any for the same reasons
template <typename T, typename... Args>
std::shared_ptr<T> shared_ptr_builder(Args... args)
{
    return std::make_shared<T>(args...);
}

}
}