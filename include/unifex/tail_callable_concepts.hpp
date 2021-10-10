/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/config.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/detail/unifex_fwd.hpp>
#include <unifex/type_list.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/manual_lifetime_union.hpp>

#include <algorithm>
#include <type_traits>
#include <optional>

#include <unifex/detail/prologue.hpp>

namespace unifex {

    template<bool... BN>
    inline constexpr bool all_true = (BN && ...);

    template<bool... BN>
    inline constexpr bool any_true = (BN || ...);

    template<typename T, typename... Ts>
    UNIFEX_CONCEPT
      one_of = any_true<same_as<T, Ts>...>;

    template<typename T>
    UNIFEX_CONCEPT 
      non_void = (!std::is_void_v<T>);

    template<typename T, typename Self>
    UNIFEX_CONCEPT 
      same_base = same_as<remove_cvref_t<T>, Self>;

    template<typename T>
    UNIFEX_CONCEPT 
      decay_copyable = constructible_from<remove_cvref_t<T>, T>;

    template<typename T>
    inline constexpr bool is_nothrow_decay_copyable_v =
        std::is_nothrow_constructible_v<remove_cvref_t<T>, T>;

    template<typename T>
    UNIFEX_CONCEPT 
      trivially_copyable =
        std::is_trivially_copy_constructible_v<T> &&
        std::is_trivially_move_constructible_v<T> &&
        std::is_trivially_destructible_v<T>;

    template <typename T>
    UNIFEX_CONCEPT_FRAGMENT( //
      _default_initializable, //
        requires() //
        (
          (T{})
        ));

    template<typename T>
    UNIFEX_CONCEPT 
      default_initializable =
        UNIFEX_FRAGMENT(unifex::_default_initializable, T);

    template <typename T>
    UNIFEX_CONCEPT_FRAGMENT( //
      _contextually_convertible_to_bool, //
        requires(const T c) //
        (
          (static_cast<const T&&>(c) ? (void)0 : (void)0)
        ));

    template<typename T>
    UNIFEX_CONCEPT 
      contextually_convertible_to_bool =
        UNIFEX_FRAGMENT(unifex::_contextually_convertible_to_bool, T);

    template <typename T>
    UNIFEX_CONCEPT_FRAGMENT( //
      _nothrow_contextually_convertible_to_bool, //
        requires(const T c) //
        (
          (static_cast<const T&&>(c) ? (void)0 : (void)0)
        ) &&
        noexcept((std::declval<const T&&>() ? (void)0 : (void)0)));

    template<typename T>
    UNIFEX_CONCEPT 
      nothrow_contextually_convertible_to_bool =
        contextually_convertible_to_bool<T> &&
        UNIFEX_FRAGMENT(unifex::_nothrow_contextually_convertible_to_bool, T);

    template<typename T>
    UNIFEX_CONCEPT 
      pass_by_value =
        trivially_copyable<T> && (sizeof(T) <= (2 * sizeof(void*)));

    template<typename T>
    struct decay_copy_fn {
        T value;

        explicit decay_copy_fn(T x) noexcept : value(static_cast<T&&>(x)) {}

        std::decay_t<T> operator()() && noexcept {//HOW: is_nothrow_convertible_v<T, std::decay_t<T>>) {
            return static_cast<T&&>(value);
        }
    };

    template(typename T)
        (requires !pass_by_value<remove_cvref_t<T>>)
    decay_copy_fn(T&&) -> decay_copy_fn<T&&>;

    template(typename T)
        (requires pass_by_value<T>)
    decay_copy_fn(T) -> decay_copy_fn<T>;

    template(typename T, typename... Ts)
        (requires one_of<T, Ts...>)
    constexpr std::size_t index_of() {
        constexpr std::size_t null = std::numeric_limits<std::size_t>::max();
        std::size_t i = 0;
        return std::min({
            (same_as<T, Ts> ? i++ : (++i, null))...
        });
    }

    template<typename... Ts>
    inline constexpr std::size_t index_of_v = index_of<Ts...>();

    static_assert(index_of_v<int, char, bool, int, void, void*> == 2);

    struct _types_are_unique {

        template(typename... Ts)
          (requires sizeof...(Ts) == 0)
        static inline constexpr bool _value() { return true; }

        template(typename T, typename... Rest)
            (requires one_of<T, Rest...>)
        static inline constexpr bool _value() { return false; }

        template<typename T, typename... Rest>
        static inline constexpr bool _value() { return _value<Rest...>(); }

        template<typename... Ts>
        static inline constexpr bool value = _value<Ts...>();
    };

    template<std::size_t N, typename... Ts>
    UNIFEX_CONCEPT 
      _nth_type_valid = (N < sizeof...(Ts));

    template<std::size_t N>
    UNIFEX_CONCEPT 
      _nth_type_found = (N == 0);

    struct _nth_type {

        template<std::size_t N, typename T, typename... Rest>
        struct _type_next;

        template(std::size_t N, typename T, typename... Rest)
          (requires _nth_type_valid<N, T, Rest...> && _nth_type_found<N>)
        static inline constexpr T _type();

        template(std::size_t N, typename T, typename... Rest)
          (requires _nth_type_valid<N, T, Rest...> && (!_nth_type_found<N>))
        static inline constexpr auto _type() -> typename _type_next<N, T, Rest...>::type;

        template<std::size_t N, typename T, typename... Rest>
        struct _type_next {
            using type = decltype(_type<N-1, Rest...>());
        };

        template<std::size_t N, typename... Ts>
        using type = decltype(_type<N, Ts...>());

    };

    template<std::size_t N, typename... Ts>
    using nth_type_t = typename _nth_type::type<N, Ts...>;

    template<typename... Ts>
    inline constexpr bool types_are_unique_v = _types_are_unique::value<Ts...>;

    template<typename... Ts>
    struct types_are_unique : std::bool_constant<types_are_unique_v<Ts...>> {};

    //////////////////////////////////////////////////
    // Tail Callable Concepts

    template <typename T>
    UNIFEX_CONCEPT_FRAGMENT( //
      _tail_callable_destroy, //
        requires(const T c) //
        (
          c.destroy()
        ) &&
        noexcept(std::declval<const T>().destroy()) &&
        same_as<decltype(std::declval<const T>().destroy()), void>);

    template<typename T>
    UNIFEX_CONCEPT
      _tail_callable_impl =
        copyable<T> &&
        std::is_nothrow_copy_constructible_v<T> &&
        std::is_nothrow_move_constructible_v<T> &&
        std::is_nothrow_copy_assignable_v<T> &&
        std::is_nothrow_move_assignable_v<T> &&
        std::is_trivially_destructible_v<T> &&
        UNIFEX_FRAGMENT(unifex::_tail_callable_destroy, T);

    template <typename T>
    UNIFEX_CONCEPT_FRAGMENT( //
      _tail_callable_invoke, //
        requires(const T c) //
        (
          c.invoke()
        ) &&
        noexcept(std::declval<const T>().invoke()) &&
        _tail_callable_impl<decltype(std::declval<const T>().invoke())> );

    template<typename T>
    UNIFEX_CONCEPT
      _tail_callable_invoke_impl =
        UNIFEX_FRAGMENT(unifex::_tail_callable_invoke, T);

    template <typename T>
    UNIFEX_CONCEPT_FRAGMENT( //
      _terminal_tail_callable_invoke, //
        requires(const T c) //
        (
          c.invoke()
        ) &&
        noexcept(std::declval<const T>().invoke()) &&
        same_as<decltype(std::declval<const T>().invoke()), void>);

    template<typename T>
    UNIFEX_CONCEPT 
      _terminal_tail_callable =
        UNIFEX_FRAGMENT(unifex::_terminal_tail_callable_invoke, T);

    template <typename T, typename... ValidTailCallable>
    UNIFEX_CONCEPT_FRAGMENT( //
      _recursive_tail_callable_invoke, //
        requires(const T c) //
        (
          c.invoke()
        ) &&
        noexcept(std::declval<const T>().invoke()) &&
        one_of<decltype(std::declval<const T>().invoke()), ValidTailCallable...>);

    template<typename T, typename... ValidTailCallable>
    UNIFEX_CONCEPT 
      _recursive_tail_callable =
        UNIFEX_FRAGMENT(unifex::_recursive_tail_callable_invoke, T, ValidTailCallable...);


    struct null_tail_callable;

    template<typename T>
    using next_tail_callable_t = decltype(std::declval<const T&>().invoke());

    template<typename T>
    UNIFEX_CONCEPT 
      _nullable_tail_callable =
        default_initializable<T> &&
        nothrow_contextually_convertible_to_bool<T>;

    template<typename... Cs>
    struct _variant_tail_callable;

    // This handles the potential for recursion 
    struct _has_tail_callable_invoke_impl {

        template(typename T, typename... PrevTailCallables)
            (requires
                (!std::is_void_v<T>) AND
                (!same_as<null_tail_callable, T>) AND
                (!_recursive_tail_callable<T, PrevTailCallables...>) AND
                (!_tail_callable_impl<T>))
        static inline constexpr bool _value() noexcept { return false; }

        template(typename T, typename... PrevTailCallables)
            (requires
                (!std::is_void_v<T>) AND
                (!same_as<null_tail_callable, T>) AND
                (!_recursive_tail_callable<T, PrevTailCallables...>) AND
                _tail_callable_impl<T>)
        static inline constexpr bool _value() noexcept {
            return _has_tail_callable_invoke_impl::_value<next_tail_callable_t<T>, T, PrevTailCallables...>();
        }

        template(typename T, typename... PrevTailCallables)
            (requires 
                std::is_void_v<T> AND
                (!same_as<null_tail_callable, T>) AND
                (!_recursive_tail_callable<T, PrevTailCallables...>) AND
                (!_tail_callable_impl<T>))
        static inline constexpr bool _value() noexcept { return true; }

        template(typename T, typename... PrevTailCallables)
            (requires 
                (!std::is_void_v<T>) AND
                same_as<null_tail_callable, remove_cvref_t<T>> AND
                (!_recursive_tail_callable<T, PrevTailCallables...>) AND
                _tail_callable_impl<T>)
        static inline constexpr bool _value() noexcept { return true; }

        template(typename T, typename... PrevTailCallables)
            (requires 
                (!std::is_void_v<T>) AND
                (!same_as<null_tail_callable, T>) AND
                _recursive_tail_callable<T, PrevTailCallables...> AND
                _tail_callable_impl<T>)
        static inline constexpr bool _value() noexcept { return true; }

        template<typename... Cs, typename... PrevTailCallables>
        static inline constexpr bool _variant_value(_variant_tail_callable<Cs...>*) noexcept {
            return _has_tail_callable_invoke_impl::_value<Cs..., _variant_tail_callable<Cs...>, PrevTailCallables...>();
        }

        template(typename T, typename... PrevTailCallables)
            (requires 
                (!std::is_void_v<T>) AND
                (!same_as<null_tail_callable, T>) AND
                (!_recursive_tail_callable<T, PrevTailCallables...>) AND
                _tail_callable_impl<T>)
        static inline constexpr auto _value() noexcept 
          -> decltype(_has_tail_callable_invoke_impl::_variant_value<T, PrevTailCallables...>(static_cast<T*>(nullptr))) {
            return _has_tail_callable_invoke_impl::_variant_value<T, PrevTailCallables...>(static_cast<T*>(nullptr));
        }
    };

    template<typename T, typename... PrevTailCallables>
    inline constexpr bool _has_tail_callable_invoke = _has_tail_callable_invoke_impl::template _value<T, PrevTailCallables...>();

    template<typename T>
    UNIFEX_CONCEPT 
      tail_callable = _tail_callable_impl<T> && _has_tail_callable_invoke<T>;

    template<typename T>
    UNIFEX_CONCEPT 
      terminal_tail_callable = tail_callable<T> && _terminal_tail_callable<T>;

    template<typename T>
    UNIFEX_CONCEPT 
      nullable_tail_callable =
        tail_callable<T> &&
        _nullable_tail_callable<T>;

    struct any_tail_callable {
        using invoke_fn = any_tail_callable(void*) noexcept;
        using destroy_fn = void(void*) noexcept;
        struct vtable {
            invoke_fn* invoke;
            destroy_fn* destroy;
        };

        any_tail_callable() noexcept = default;

        explicit any_tail_callable(const vtable& vt, void* data) noexcept
        : vt(std::addressof(vt)), data(data) {}

        constexpr explicit operator bool() const noexcept { return vt != nullptr; }
        any_tail_callable invoke() const noexcept { return vt->invoke(data); }
        void destroy() const noexcept { vt->destroy(data); }

    private:
        const vtable* vt{nullptr};
        void* data{nullptr};
    };

    struct null_tail_callable {
        operator any_tail_callable() const noexcept { return any_tail_callable(); }
        constexpr explicit operator bool() const noexcept { return false; }
        [[noreturn]] void destroy() const noexcept {
            std::terminate();
        }
        [[noreturn]] void invoke() const noexcept {
            std::terminate();
        }
    };

    template(typename C)
      (requires tail_callable<C>)
    auto _resume_until_nullable(C c) {
        if constexpr (nullable_tail_callable<C>) {
            return c;
        } else {
            auto c2 = c.invoke();
            return _resume_until_nullable(c2);
        }
    }

    template(typename C)
      (requires tail_callable<C>)
    auto _invoke_until_nullable(C c) {
        if constexpr (nullable_tail_callable<C>) {
            return c;
        } else if constexpr (_terminal_tail_callable<C>) {
            c.invoke();
            return null_tail_callable{};
        } else {
            return _invoke_until_nullable(c.invoke());
        }
    }

    template<typename C, typename... Prev>
    auto _invoke_sequential(C c, type_list<Prev...>) {
        static_assert(tail_callable<C>, "_invoke_sequential: must be called with a tail_callable");
        if constexpr (_terminal_tail_callable<C>) {
            if constexpr (nullable_tail_callable<C>) {
                return c;
            } else {
                c.invoke();
                return null_tail_callable{};
            }
        } else {
          auto next = c.invoke();
          using next_t = decltype(next);
          if constexpr (one_of<next_t, C, Prev...>) {
              static_assert((nullable_tail_callable<C> || (nullable_tail_callable<Prev> || ...)),
                  "At least one tail_callable in a cycle must be nullable to avoid entering an infinite loop");
              return _invoke_until_nullable(next);
          } else {
              using result_type = decltype(_invoke_sequential(next, type_list<C, Prev...>{}));    
              if constexpr (same_as<result_type, next_t>) {
                  // Let the loop in resume_tail_callable() handle checking the boolean.
                  return next;
              } else {
                  if constexpr (nullable_tail_callable<next_t>) {
                      if (!next) {
                          return result_type{};
                      }
                  }
                  return _invoke_sequential(next, type_list<C, Prev...>{});
              }
          }
        }
    }

    template(typename C)
      (requires tail_callable<C>)
    auto _invoke_sequential(C c) {
        static_assert(tail_callable<C>, "_invoke_sequential: must be called with a tail_callable");
        return _invoke_sequential(c, type_list<>{});
    }


    template(typename C)
      (requires tail_callable<C>)
    void resume_tail_callable(C c) {
        static_assert(nullable_tail_callable<decltype(_invoke_sequential(c))>, "resume_tail_callable: _invoke_sequential must return a nullable_tail_callable");
        auto c2 = _invoke_sequential(c);
        while (c2) {
            c2 = _invoke_sequential(c2, type_list<>{});
        }
    }

    template(typename... Cs)
      (requires all_true<tail_callable<Cs>...>)
    void resume_tail_callables(Cs... cs) {
        static_assert(all_true<nullable_tail_callable<decltype(_invoke_sequential(cs))>...>, "resume_tail_callables: _invoke_sequential must return a nullable_tail_callable");
        auto c2sTuple = std::make_tuple(_invoke_sequential(cs)...);
        while (std::apply([](auto... c2s) noexcept { return (c2s || ...); }, c2sTuple)) {
            c2sTuple = std::apply([](auto... c2s) noexcept {
                return std::make_tuple(_invoke_sequential(c2s)...);
            }, c2sTuple);
        }
    }

    template(typename C)
        (requires tail_callable<C> && (!nullable_tail_callable<C>))
    struct maybe_tail_callable {
        maybe_tail_callable() noexcept = default;
        maybe_tail_callable(null_tail_callable) noexcept {}
        maybe_tail_callable(C c) noexcept : tail_callable(c) {}
        operator any_tail_callable() const noexcept {
            return tail_callable ? any_tail_callable(*tail_callable) : any_tail_callable();
        }
        explicit operator bool() const noexcept { return tail_callable.has_value(); }
        void destroy() const noexcept { tail_callable->destroy(); }
        auto invoke() const noexcept { tail_callable->invoke(); }
    private:
        std::optional<C> tail_callable;
    };

    struct _first_nullable_tail_callable {

        template<typename C, typename... Rest>
        struct _type_next;

        template(typename C, typename... Rest)
          (requires nullable_tail_callable<C> && all_true<tail_callable<Rest>...>)
        static inline constexpr C _type();

        template(typename C, typename... Rest)
            (requires !nullable_tail_callable<C> && all_true<tail_callable<Rest>...>)
        static inline constexpr auto _type() -> typename _type_next<C, Rest...>::type;

        template<typename C, typename... Rest>
        struct _type_next {
            using type = decltype(_type<Rest...>());
        };

        template<typename... Cs>
        using type = decltype(_type<Cs...>());
    };

    template<typename... Cs>
    using first_nullable_tail_callable_t = typename _first_nullable_tail_callable::type<Cs...>;


    template<typename C>
    struct _flatten_variant_element {
        using type = type_list<C>;
    };

    template<typename... Cs>
    struct _flatten_variant_element<_variant_tail_callable<Cs...>> {
        using type = type_list<Cs...>;
    };

    template<typename... Cs>
    struct _variant_or_single {
        using type = _variant_tail_callable<Cs...>;
    };

    template<typename C>
    struct _variant_or_single<C> {
        using type = C;
    };

    template<>
    struct _variant_or_single<> {
        using type = null_tail_callable;
    };

    template<typename T>
    using replace_void_with_null_tail_callable =
        std::conditional_t<std::is_void_v<T>, null_tail_callable, T>;

    template<typename... Cs>
    using variant_tail_callable = typename concat_type_lists_unique_t<
        typename _flatten_variant_element<replace_void_with_null_tail_callable<Cs>>::type...>::template apply<_variant_or_single>::type;

    template<typename CPO, typename Target, typename... Args>
    auto result_or_null_tail_callable(CPO cpo, Target&& t, Args&&... args) {
      if constexpr (std::is_void_v<callable_result_t<CPO, Target, Args...>>) {
        cpo((Target&&)t, (Args&&)args...);
        return null_tail_callable{};
      } else {
        return cpo((Target&&)t, (Args&&)args...);
      }
    }

    template<typename... Cs>
    struct _variant_tail_callable {
        static_assert(sizeof...(Cs) >= 2);
        static_assert(types_are_unique_v<Cs...>);

        _variant_tail_callable() noexcept
        : _variant_tail_callable(first_nullable_tail_callable_t<replace_void_with_null_tail_callable<Cs>...>{})
        {
        }

        template(typename C)
          (requires one_of<C, replace_void_with_null_tail_callable<Cs>...>)
        _variant_tail_callable(C c) noexcept
        : tag(index_of_v<C, replace_void_with_null_tail_callable<Cs>...>) {
            state.template construct<C>(c);
        }

        _variant_tail_callable(const _variant_tail_callable&) noexcept = default;
        _variant_tail_callable& operator=(const _variant_tail_callable&) noexcept = default;

        template(typename... OtherCs)
            (requires  all_true<one_of<replace_void_with_null_tail_callable<OtherCs>, replace_void_with_null_tail_callable<Cs>...>...>)
        _variant_tail_callable(_variant_tail_callable<OtherCs...> c) noexcept {
            c.visit([this](auto other_c) noexcept {
                *this = _variant_tail_callable(other_c);
            });
        }

        template(typename... Args)
          (requires (contextually_convertible_to_bool<Cs> || ... ) && (sizeof...(Args) == 0))
        explicit operator bool() const noexcept {
            bool result = true;
            visit([&result](auto c) noexcept {
                if constexpr (contextually_convertible_to_bool<decltype(c)>) {
                    static_assert(noexcept(static_cast<bool>(c)));
                    result = static_cast<bool>(c);
                }
            });
            return result;
        }

        template(typename... Args)
          (requires (sizeof...(Args) == 0))
        auto invoke(Args...) const noexcept {
            using invoke_result_tail_callable = variant_tail_callable<
                null_tail_callable,
                decltype(std::declval<replace_void_with_null_tail_callable<Cs>>().invoke())...>;
            return visit([](auto c) noexcept -> invoke_result_tail_callable {
                if constexpr (_terminal_tail_callable<decltype(c)>) {
                    static_assert(std::is_void_v<decltype(c.invoke())>);
                    c.invoke();
                    return {null_tail_callable{}};
                } else {
                    return {c.invoke()};
                }
            });
        }

        template(typename... Args)
          (requires (sizeof...(Args) == 0))
        void destroy(Args...) const noexcept {
            visit([](auto c) { c.destroy(); });
        }

    private:
        template<typename... OtherCs>
        friend struct _variant_tail_callable;

        template<typename F>
        auto visit(F f) const noexcept {
            return visit_impl(std::move(f), std::index_sequence_for<Cs...>{});
        }

        template<typename F, std::size_t Idx, std::size_t... Indices>
        auto visit_impl(F f, std::index_sequence<Idx, Indices...>) const noexcept {
            using T = nth_type_t<Idx, replace_void_with_null_tail_callable<Cs>...>;
            if constexpr (sizeof...(Indices) == 0) {
                return f(state.template get<T>());
            } else if (tag == Idx) {
                return f(state.template get<T>());
            } else {
                return visit_impl(std::move(f), std::index_sequence<Indices...>{});
            }
        }
        
        std::size_t tag;
        manual_lifetime_union<replace_void_with_null_tail_callable<Cs>...> state;
    };

    inline constexpr null_tail_callable resume_tail_callables_until_one_remaining() noexcept {
        return {};
    }

    template(typename C)
      (requires tail_callable<C>)
    C resume_tail_callables_until_one_remaining(C c) noexcept {
        return c;
    }

    template(typename C0, typename C1, typename... Cs)
      (requires tail_callable<C0> && tail_callable<C1> && all_true<tail_callable<Cs>...>)
    auto resume_tail_callables_until_one_remaining(C0 c0, C1 c1, Cs... cs) noexcept {
        using result_type = variant_tail_callable<decltype(_invoke_sequential(c0)), decltype(_invoke_sequential(c1)), decltype(_invoke_sequential(cs))...>;
        result_type result;

        auto cs2_tuple = std::make_tuple(_invoke_sequential(c0), _invoke_sequential(c1), _invoke_sequential(cs)...);
        while (true) {
            std::size_t remaining = sizeof...(cs) + 2;
            std::apply([&](auto&... c2s) noexcept {
                (
                    (c2s ? (void)(result = c2s = _invoke_sequential(c2s)) : (void)--remaining), ...
                );
            }, cs2_tuple);

            if (remaining <= 1) {
                return result;
            }
        }
    }

    template(typename C)
      (requires tail_callable<C>)
    struct scoped_tail_callable {
        explicit scoped_tail_callable(C c) noexcept
        : cont(c), valid(true) {}

        scoped_tail_callable(scoped_tail_callable&& other) noexcept
        : cont(other.cont)
        , valid(std::exchange(other.valid, false))
        {}

        ~scoped_tail_callable() {
            if (valid) {
                cont.destroy();
            }
        }

        C get() noexcept { return cont; }

        C release() noexcept {
            valid = false;
            return cont;
        }

    private:
        C cont;
        bool valid;
    };

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
