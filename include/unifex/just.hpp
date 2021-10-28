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
#include <unifex/blocking.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _just {

template <typename Receiver, typename... Values>
struct _op {
  struct type;
};
template <typename Receiver, typename... Values>
using operation = typename _op<remove_cvref_t<Receiver>, Values...>::type;

template <typename Receiver, typename... Values>
struct _op<Receiver, Values...>::type {
  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  type(const type&) = delete;
  type(type&&) = delete;
  type operator=(const type&) = delete;
  type operator=(type&&) = delete;

  using tail_t = variant_tail_callable<
      null_tail_callable,
      callable_result_t<tag_t<unifex::set_value>, Receiver, Values...>,
      callable_result_t<
          tag_t<unifex::set_error>,
          Receiver,
          std::exception_ptr>>;

  tail_t start() & noexcept {
    UNIFEX_TRY {
      return {std::apply(
          [&](Values&&... values) -> tail_t {
            using tail = callable_result_t<
                tag_t<unifex::set_value>,
                Receiver,
                Values...>;
            if constexpr (sender<tail>) {
              static_assert(!sender<tail>, "just: sender not yet supported");
            } else {
              return result_or_null_tail_callable(
                  unifex::set_value,
                  (Receiver &&) receiver_,
                  (Values &&) values...);
            }
          },
          std::move(values_))};
    }
    UNIFEX_CATCH(...) {
      using tail = callable_result_t<
          tag_t<unifex::set_error>,
          Receiver,
          std::exception_ptr>;
      if constexpr (sender<tail>) {
        static_assert(!sender<tail>, "just: sender not yet supported");
      } else {
        return {result_or_null_tail_callable(
            unifex::set_error,
            (Receiver &&) receiver_,
            std::current_exception())};
      }
    }
    return {null_tail_callable{}};
  }
};

template <typename... Values>
struct _sender {
  class type;
};
template <typename... Values>
using sender = typename _sender<std::decay_t<Values>...>::type;

template <typename... Values>
class _sender<Values...>::type {
  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;

public:
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<Values...>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = false;

  template(typename... Values2)                       //
      (requires                                       //
       (sizeof...(Values2) == sizeof...(Values)) AND  //
       (constructible_from<
           std::tuple<Values...>,
           Values2...>))                                   //
      explicit type(std::in_place_t, Values2&&... values)  //
      noexcept(
          std::is_nothrow_constructible_v<std::tuple<Values...>, Values2...>)
    : values_((Values2 &&) values...) {}

  template(typename This, typename Receiver)                               //
      (requires                                                            //
       (same_as<remove_cvref_t<This>, type>) AND                           //
       (receiver<Receiver>) AND                                            //
       (is_callable_v<tag_t<unifex::set_value>, Receiver, Values...>) AND  //
       (constructible_from<
           std::tuple<Values...>,
           member_t<
               This,
               std::tuple<Values...>>>))  //
      friend auto tag_invoke(
          tag_t<connect>,
          This&& that,
          Receiver&& r)  //
      noexcept(          //
          (is_nothrow_callable_v<
              tag_t<unifex::set_value>,
              Receiver,
              Values...>)&&  //
          (std::is_nothrow_constructible_v<
              std::tuple<Values...>,
              member_t<This, std::tuple<Values...>>>))
          -> operation<Receiver, Values...> {
    return {static_cast<This&&>(that).values_, static_cast<Receiver&&>(r)};
  }

  friend constexpr blocking_kind
  tag_invoke(tag_t<blocking>, const type&) noexcept {
    return blocking_kind::always_inline;
  }
};
}  // namespace _just

namespace _just_cpo {
inline const struct just_fn {
  template <typename... Values>
  constexpr auto operator()(Values&&... values) const
      noexcept(std::is_nothrow_constructible_v<
               _just::sender<Values...>,
               std::in_place_t,
               Values...>) -> _just::sender<Values...> {
    return _just::sender<Values...>{std::in_place, (Values &&) values...};
  }
} just{};
}  // namespace _just_cpo
using _just_cpo::just;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
