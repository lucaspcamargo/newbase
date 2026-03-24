#pragma once

#include <entt/meta/meta.hpp>
#include <memory>
#include <tuple>
#include <type_traits>

namespace nb {

namespace detail {

template<typename T>
struct fn_traits : fn_traits<decltype(&T::operator())> {};
template<typename R, typename... Args>
struct fn_traits<R(*)(Args...)>           { using args = std::tuple<Args...>; };
template<typename C, typename R, typename... Args>
struct fn_traits<R(C::*)(Args...) const>  { using args = std::tuple<Args...>; };
template<typename C, typename R, typename... Args>
struct fn_traits<R(C::*)(Args...)>        { using args = std::tuple<Args...>; };

template<typename Fn, typename ArgTuple, size_t... Is>
void call_with_meta_args(Fn &fn, entt::meta_any *args, std::index_sequence<Is...>)
{
    fn((*args[Is].try_cast<std::tuple_element_t<Is, ArgTuple>>())...);
}

} // namespace detail

// Type-erased owning callable. Arguments are passed as entt::meta_any, so
// the signature is not part of the type — no per-signature registration needed.
struct meta_callback {
    std::shared_ptr<void> capture;
    entt::meta_any (*invoke_fn)(void *capture, entt::meta_any *args, size_t n) {nullptr};

    bool valid() const { return invoke_fn != nullptr; }

    entt::meta_any invoke(entt::meta_any *args, size_t n) const
    {
        return invoke_fn(capture.get(), args, n);
    }

    // Convenience: call with typed args — they are wrapped into meta_any automatically.
    template<typename... Args>
    entt::meta_any operator()(Args &&...args) const
    {
        entt::meta_any arr[] = { entt::meta_any{std::forward<Args>(args)}... };
        return invoke(arr, sizeof...(Args));
    }

    // Build from any copyable C++ callable. Argument types are deduced via
    // fn_traits and unpacked from the meta_any array at call time.
    template<typename Fn>
    static meta_callback from_fn(Fn &&fn)
    {
        using StoredFn  = std::decay_t<Fn>;
        using ArgTuple  = typename detail::fn_traits<StoredFn>::args;
        constexpr size_t N = std::tuple_size_v<ArgTuple>;

        auto cap = std::make_shared<StoredFn>(std::forward<Fn>(fn));
        return {
            cap,
            +[](void *cap, entt::meta_any *args, size_t) -> entt::meta_any {
                detail::call_with_meta_args<StoredFn, ArgTuple>(
                    *static_cast<StoredFn *>(cap), args, std::make_index_sequence<N>{});
                return {};
            }
        };
    }
};

} // namespace nb
