#include <newbase/audio/res/rlpcvocab.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/reflection/data.hpp>
#include <newbase/log.hpp>
#include <entt/meta/factory.hpp>
#include <entt/core/hashed_string.hpp>
#include "IconsForkAwesome.h"

#include <cstdint>
#include <cstring>
#include <vector>
#include <memory>

using entt::operator""_hs;

// ---------------------------------------------------------------------------
// Binary parser helpers (little-endian)
// ---------------------------------------------------------------------------

namespace {

static inline uint16_t read_u16(const uint8_t*& p, const uint8_t* end)
{
    if (p + 2 > end) return 0;
    uint16_t v = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
    p += 2;
    return v;
}

static inline uint32_t read_u32(const uint8_t*& p, const uint8_t* end)
{
    if (p + 4 > end) return 0;
    uint32_t v = static_cast<uint32_t>(p[0])
               | (static_cast<uint32_t>(p[1]) << 8)
               | (static_cast<uint32_t>(p[2]) << 16)
               | (static_cast<uint32_t>(p[3]) << 24);
    p += 4;
    return v;
}

static inline uint8_t read_u8(const uint8_t*& p, const uint8_t* end)
{
    if (p + 1 > end) return 0;
    return *p++;
}

static inline std::string read_str(const uint8_t*& p, const uint8_t* end, size_t len)
{
    if (p + len > end) { p = end; return {}; }
    std::string s(reinterpret_cast<const char*>(p), len);
    p += len;
    return s;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Loader
// ---------------------------------------------------------------------------

static std::shared_ptr<nb::rlpcvocab> load_rlpcvocab(entt::id_type id)
{
    auto res = std::make_shared<nb::rlpcvocab>(id);

    std::vector<char> raw;
    if (!nb::rman().read_all_sync(id, raw))
    {
        nb::log::warn("[rlpcvocab] could not read resource %llu", static_cast<unsigned long long>(id));
        return res;
    }

    const auto* p   = reinterpret_cast<const uint8_t*>(raw.data());
    const auto* end = p + raw.size();

    // Magic
    if (raw.size() < 10 || std::memcmp(p, "RLPV", 4) != 0)
    {
        nb::log::warn("[rlpcvocab] bad magic in resource %llu", static_cast<unsigned long long>(id));
        return res;
    }
    p += 4;

    const uint16_t version = read_u16(p, end);
    if (version != 1)
    {
        nb::log::warn("[rlpcvocab] unsupported version %u in resource %llu",
                      static_cast<unsigned>(version), static_cast<unsigned long long>(id));
        return res;
    }

    const uint32_t word_count = read_u32(p, end);
    res->words.reserve(word_count);

    for (uint32_t w = 0; w < word_count && p < end; ++w)
    {
        nb::rlpcvocab_word word;
        uint16_t name_len = read_u16(p, end);
        word.name = read_str(p, end, name_len);

        uint8_t var_count = read_u8(p, end);
        word.variants.reserve(var_count);

        for (uint8_t v = 0; v < var_count && p < end; ++v)
        {
            nb::rlpcvocab_variant var;
            uint8_t vname_len = read_u8(p, end);
            var.name = read_str(p, end, vname_len);

            uint32_t data_len = read_u32(p, end);
            if (p + data_len > end)
            {
                nb::log::warn("[rlpcvocab] truncated data for word '%s' variant '%s'",
                              word.name.c_str(), var.name.c_str());
                break;
            }
            var.data.assign(p, p + data_len);
            p += data_len;

            word.variants.push_back(std::move(var));
        }

        res->words.push_back(std::move(word));
    }

    res->valid = true;
    nb::log::verb("[rlpcvocab] loaded %zu words from resource %llu",
                  res->words.size(), static_cast<unsigned long long>(id));
    return res;
}

// ---------------------------------------------------------------------------
// RTTI registration — called from _rtti_init_audio() in audio.cpp
// ---------------------------------------------------------------------------

void _rtti_init_audio_rlpcvocab()
{
    using namespace nb::rtti;

    entt::meta_factory<nb::rlpcvocab>{}
        .type("rlpcvocab"_hs)
        .custom<rtti::type_info>(rtti::type_info{
            .identifier = "lpcvocab",
            .type_class = TYPE_CLASS_RESOURCE,
            .data = {.resource = {
                .editor_icon = ICON_FK_COMMENTS,
                .extensions  = "rlpcvocab"
            }},
            .loader_fn = +[](entt::id_type id) -> std::shared_ptr<nb::resource> {
                return load_rlpcvocab(id);
            }
        });

    // shared_ptr<rlpcvocab> registration so resource fields can reference this type in the editor
    entt::meta_factory<std::shared_ptr<nb::rlpcvocab>>{}
        .type("rlpcvocab_ptr"_hs)
        .ctor<>()
        .custom<rtti::type_info>(rtti::type_info{
            .type_class = TYPE_CLASS_RESOURCE_PTR,
            .data = {.resource_ptr = {
                .resource_type_id = "rlpcvocab"_hs.value(),
                .get_ptr = +[](const entt::meta_any& a) -> std::shared_ptr<nb::resource> {
                    auto* p = a.try_cast<std::shared_ptr<nb::rlpcvocab>>();
                    return p ? *p : nullptr;
                },
                .set_ptr = +[](entt::meta_any& a, std::shared_ptr<nb::resource> p) {
                    a.assign(std::static_pointer_cast<nb::rlpcvocab>(p));
                }
            }}
        });
}
