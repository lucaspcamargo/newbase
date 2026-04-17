#pragma once

#include <memory>
#include <cstring>
#include <entt/entt.hpp>

namespace nb { class resource; }

// These structures are used to augment the entt::meta reflection facility with some extra data 
// that is useful for our editor and scripting systems, such as type identifiers and editor icons for components,
// or writability flags for resource storage types. 
// We use entt::meta's custom() function to associate these with the reflected types, 
// and we can then query them at runtime when we need to display editor icons or bind types to lua.

// Use "safer" string copy on MSVCPP, normal otherwise...
#ifndef _MSC_VER
#define strncpy_s strncpy
#endif

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
        strncpy_s(_val, strptr, N);
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

enum type_class_t {
    TYPE_CLASS_NONE = 0,
    TYPE_CLASS_COMPONENT = 1,
    TYPE_CLASS_RESOURCE = 2,
    TYPE_CLASS_SYSTEM = 3,
    TYPE_CLASS_SINGLETON = 4,
    TYPE_CLASS_RES_STORAGE = 5,
    TYPE_CLASS_SERVICE = 6,
    TYPE_CLASS_RESOURCE_PTR = 7, // std::shared_ptr<T> where T : nb::resource
};

struct type_info
{
    cstrn<32> identifier {};

    type_class_t type_class {TYPE_CLASS_NONE};

    union {
        struct {
            const char *editor_icon;
            void (*notify)(entt::registry &, entt::entity, void*) {nullptr};
        } component;

        struct {
            bool sure_writable;
            bool maybe_writable;

            // whether the storage type surely supports, or may possibly support, scanning it for files and sizes
            bool sure_scaneable;
            bool maybe_scaneable;
        } res_storage;

        struct {
            // returns a void* to the service instance via entt::locator, or nullptr if unavailable
            void* (*getter)();
        } service;

        struct {
            const char *editor_icon; // icon glyph (e.g. from ForkAwesome) shown in the resource browser
            const char *extensions;  // space-separated list of handled file extensions (without dot, e.g. "png jpg jpeg")
        } resource;

        struct {
            // meta type id of the pointed-to resource (e.g. "rtexture"_hs.value())
            entt::id_type resource_type_id;
            // extracts the base shared_ptr<nb::resource> from a meta_any holding this type
            std::shared_ptr<nb::resource> (*get_ptr)(const entt::meta_any&);
            // writes a new base shared_ptr<nb::resource> back into the meta_any (casts to concrete type)
            void (*set_ptr)(entt::meta_any&, std::shared_ptr<nb::resource>);
        } resource_ptr;
    } data;

    void *uptr {nullptr};

    // for TYPE_CLASS_RESOURCE: load function returning a base resource pointer
    std::shared_ptr<nb::resource>(*loader_fn)(entt::id_type) {nullptr};

    // for TYPE_CLASS_RESOURCE: optional save function — serializes the in-memory resource
    // back to storage via rman().write_all_sync(). Returns false if unsupported or failed.
    bool (*saver_fn)(nb::resource*) {nullptr};

};

struct func_info
{
    cstrn<32> identifier {};
};

enum data_subtype_t {
    DATA_SUBTYPE_NONE     = 0,
    DATA_SUBTYPE_RESOURCE = 1,  // field holds a resource id (entt::id_type / hashed_string value)
    DATA_SUBTYPE_COLOR    = 2,  // glm::vec4 displayed as an RGBA color picker
};

struct data_info
{
    cstrn<32> identifier {};

    data_subtype_t subtype {DATA_SUBTYPE_NONE};

    // For DATA_SUBTYPE_RESOURCE: meta-type of the expected resource (e.g. "rtexture"_hs.value()).
    // Leave at 0 to indicate "any resource type".
    entt::id_type resource_type_id {0};
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