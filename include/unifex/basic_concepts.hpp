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

#include <unifex/std_concepts.hpp>
#include <unifex/swap.hpp>
#include <unifex/type_traits.hpp>

#include <algorithm>
#include <functional>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

template <bool... BN>
inline constexpr bool all_true = (BN && ...);

template <bool... BN>
inline constexpr bool any_true = (BN || ...);

template <typename T, typename... Ts>
UNIFEX_CONCEPT one_of = any_true<same_as<T, Ts>...>;

template <typename T>
UNIFEX_CONCEPT non_void = (!std::is_void_v<T>);

template <typename T, typename Self>
UNIFEX_CONCEPT same_base = same_as<remove_cvref_t<T>, Self>;

template <typename T>
UNIFEX_CONCEPT decay_copyable = constructible_from<remove_cvref_t<T>, T>;

template <typename T>
inline constexpr bool is_nothrow_decay_copyable_v =
    std::is_nothrow_constructible_v<remove_cvref_t<T>, T>;

template <typename T>
UNIFEX_CONCEPT trivially_copyable = std::is_trivially_copy_constructible_v<T>&&
    std::is_trivially_move_constructible_v<T>&&
        std::is_trivially_destructible_v<T>;

template <typename T>
UNIFEX_CONCEPT_FRAGMENT(     //
    _default_initializable,  //
    requires()               //
    ((T{})));

template <typename T>
UNIFEX_CONCEPT
    default_initializable = UNIFEX_FRAGMENT(unifex::_default_initializable, T);

template <typename T>
UNIFEX_CONCEPT_FRAGMENT(                //
    _contextually_convertible_to_bool,  //
    requires(const T c)                 //
    ((static_cast<const T&&>(c) ? (void)0 : (void)0)));

template <typename T>
UNIFEX_CONCEPT contextually_convertible_to_bool =
    UNIFEX_FRAGMENT(unifex::_contextually_convertible_to_bool, T);

template <typename T>
UNIFEX_CONCEPT_FRAGMENT(                        //
    _nothrow_contextually_convertible_to_bool,  //
    requires(const T c)                         //
    ((static_cast<const T&&>(c) ? (void)0 : (void)0)) && noexcept(
        (std::declval<const T&&>() ? (void)0 : (void)0)));

template <typename T>
UNIFEX_CONCEPT nothrow_contextually_convertible_to_bool =
    contextually_convertible_to_bool<T>&&
        UNIFEX_FRAGMENT(unifex::_nothrow_contextually_convertible_to_bool, T);

template <typename T>
UNIFEX_CONCEPT pass_by_value = trivially_copyable<T> &&
    (sizeof(T) <= (2 * sizeof(void*)));

template <typename T>
struct decay_copy_fn {
  T value;

  explicit decay_copy_fn(T x) noexcept : value(static_cast<T&&>(x)) {}

  std::decay_t<T>
  operator()() && noexcept {  // HOW: is_nothrow_convertible_v<T,
                              // std::decay_t<T>>) {
    return static_cast<T&&>(value);
  }
};

template(typename T)(requires !pass_by_value<remove_cvref_t<T>>)
    decay_copy_fn(T&&)
        ->decay_copy_fn<T&&>;

template(typename T)(requires pass_by_value<T>) decay_copy_fn(T)
    ->decay_copy_fn<T>;

template(typename T, typename... Ts)(
    requires one_of<T, Ts...>) constexpr std::size_t index_of() {
  constexpr std::size_t null = std::numeric_limits<std::size_t>::max();
  std::size_t i = 0;
  return std::min({(same_as<T, Ts> ? i++ : (++i, null))...});
}

template <typename... Ts>
inline constexpr std::size_t index_of_v = index_of<Ts...>();

static_assert(index_of_v<int, char, bool, int, void, void*> == 2);

struct _types_are_unique {
  template(typename... Ts)(
      requires sizeof...(Ts) == 0) static inline constexpr bool _value() {
    return true;
  }

  template(typename T, typename... Rest)(
      requires one_of<T, Rest...>) static inline constexpr bool _value() {
    return false;
  }

  template <typename T, typename... Rest>
  static inline constexpr bool _value() {
    return _value<Rest...>();
  }

  template <typename... Ts>
  static inline constexpr bool value = _value<Ts...>();
};

template <std::size_t N, typename... Ts>
UNIFEX_CONCEPT _nth_type_valid = (N < sizeof...(Ts));

template <std::size_t N>
UNIFEX_CONCEPT _nth_type_found = (N == 0);

struct _nth_type {
  template <std::size_t N, typename T, typename... Rest>
  struct _type_next;

  template(std::size_t N, typename T, typename... Rest)(
      requires _nth_type_valid<N, T, Rest...>&&
          _nth_type_found<N>) static inline constexpr T _type();

  template(std::size_t N, typename T, typename... Rest)(
      requires _nth_type_valid<N, T, Rest...> &&
      (!_nth_type_found<N>)) static inline constexpr auto _type() ->
      typename _type_next<N, T, Rest...>::type;

  template <std::size_t N, typename T, typename... Rest>
  struct _type_next {
    using type = decltype(_type<N - 1, Rest...>());
  };

  template <std::size_t N, typename... Ts>
  using type = decltype(_type<N, Ts...>());
};

template <std::size_t N, typename... Ts>
using nth_type_t = typename _nth_type::type<N, Ts...>;

template <typename... Ts>
inline constexpr bool types_are_unique_v = _types_are_unique::value<Ts...>;

template <typename... Ts>
struct types_are_unique : std::bool_constant<types_are_unique_v<Ts...>> {};
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
