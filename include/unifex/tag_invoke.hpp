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
#include <unifex/type_traits.hpp>
#include <unifex/detail/concept_macros.hpp>

#include <unifex/std_concepts.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _tag_invoke {
void tag_invoke();

// constexpr_value_of<T>() is a complete hack that threads the language rules
// to get a compile-time constant of a value with unknown constructors
// - std::declval<T>() was not working, this does
//
// WARNING: completely UB to reference the value, it is not constructed
template <typename T>
union _u {
  // need to specify destructor and constructor to do nothing
  ~_u() noexcept {}
  constexpr _u() : i_(0) {}
  // need a type that can be constructed at compile time
  int i_;
  // specify the type that may not be constructible
  std::remove_reference_t<T> t_;
};
template <typename T>
static inline _u<T> u_{};

template <typename T>
constexpr auto& constexpr_value_of() {
  return u_<T>.t_;
}

// constexpr_value<T> allows a tag_invoke customization to support constexpr
// invocation constexpr auto tag_invoke(constexpr_value<CPO>,
// constexpr_value<const Target&>) {..}
template <typename T>
struct _constexpr_value {
  using type = T;
  constexpr _constexpr_value() noexcept {}
  constexpr _constexpr_value(T&) noexcept {}
};

template <typename CPO, typename Target, typename... As>
inline constexpr auto _v = tag_invoke(
    constexpr_value_of<CPO>(),
    constexpr_value_of<Target>(),
    constexpr_value_of<As>()...);

struct _fn {
  template <typename CPO, typename... Args>
  constexpr auto operator()(CPO cpo, Args&&... args) const
      noexcept(noexcept(tag_invoke((CPO &&) cpo, (Args &&) args...)))
          -> decltype(tag_invoke((CPO &&) cpo, (Args &&) args...)) {
    return tag_invoke((CPO &&) cpo, (Args &&) args...);
  }
};

template <typename CPO, typename... Args>
using tag_invoke_result_t =
    decltype(tag_invoke(UNIFEX_DECLVAL(CPO &&), UNIFEX_DECLVAL(Args&&)...));

using yes_type = char;
using no_type = char (&)[2];

template <typename CPO, typename... Args>
constexpr auto try_tag_invoke(int)  //
    noexcept(
        noexcept(tag_invoke(UNIFEX_DECLVAL(CPO&&), UNIFEX_DECLVAL(Args&&)...)))
        -> decltype(
            static_cast<void>(
                tag_invoke(UNIFEX_DECLVAL(CPO &&), UNIFEX_DECLVAL(Args&&)...)),
            yes_type{});

template <typename CPO, typename... Args>
constexpr no_type try_tag_invoke(...) noexcept(false);

template <template <typename...> class T, typename... Args>
struct defer {
  using type = T<Args...>;
};

struct empty {};
}  // namespace _tag_invoke

namespace _tag_invoke_cpo {
inline constexpr _tag_invoke::_fn tag_invoke{};
}
using namespace _tag_invoke_cpo;

template <typename T>
using constexpr_value = _tag_invoke::_constexpr_value<T>;

template <auto& CPO>
using tag_t = remove_cvref_t<decltype(CPO)>;

// Manually implement the traits here rather than defining them in terms of
// the corresponding std::invoke_result/is_invocable/is_nothrow_invocable
// traits to improve compile-times. We don't need all of the generality of the
// std:: traits and the tag_invoke traits are used heavily through libunifex
// so optimising them for compile time makes a big difference.

using _tag_invoke::tag_invoke_result_t;

template <typename CPO, typename... Args>
inline constexpr bool is_tag_invocable_v =
    (sizeof(_tag_invoke::try_tag_invoke<CPO, Args...>(0)) ==
     sizeof(_tag_invoke::yes_type));

template <typename CPO, typename... Args>
struct tag_invoke_result
  : conditional_t<
        is_tag_invocable_v<CPO, Args...>,
        _tag_invoke::defer<tag_invoke_result_t, CPO, Args...>,
        _tag_invoke::empty> {};

template <typename CPO, typename... Args>
using is_tag_invocable = std::bool_constant<is_tag_invocable_v<CPO, Args...>>;

template <typename CPO, typename... Args>
inline constexpr bool is_nothrow_tag_invocable_v =
    noexcept(_tag_invoke::try_tag_invoke<CPO, Args...>(0));

template <typename CPO, typename... Args>
using is_nothrow_tag_invocable =
    std::bool_constant<is_nothrow_tag_invocable_v<CPO, Args...>>;

template <typename CPO, typename... Args>
UNIFEX_CONCEPT tag_invocable =
    (sizeof(_tag_invoke::try_tag_invoke<CPO, Args...>(0)) ==
     sizeof(_tag_invoke::yes_type));

template <typename CPO, typename Target, typename... As>
inline constexpr auto tag_invoke_v = _tag_invoke::_v<CPO, Target, As...>;

template <typename Fn>
using meta_tag_invoke_result =
    meta_quote1_<tag_invoke_result_t>::bind_front<Fn>;

// tag_invoke_member is a CPO customized by other CPOs to define named object
// methods
namespace _tag_invoke_member {
struct _fn {
  template <typename CPO, typename Target, typename... ArgN>
  constexpr auto operator()(CPO, Target&& t, ArgN&&... argn) const {
    return tag_invoke(*this, CPO{}, (Target &&) t, (ArgN &&) argn...);
  }
};
inline constexpr _fn tag_invoke_member{};
}  // namespace _tag_invoke_member
using _tag_invoke_member::tag_invoke_member;

// implement tag_invoke to tag_invoke_member for Derived
//
template <typename Derived, typename Cpo>
struct _tag_invoke_member_base {
  template(typename Target, typename... ArgN)                                //
      (requires std::is_base_of_v<Derived, unifex::remove_cvref_t<Target>>)  //
      friend constexpr auto tag_invoke(
          Cpo,
          Target&& t,
          ArgN&&... argn)  //
      noexcept(            //
          unifex::is_nothrow_tag_invocable_v<
              unifex::tag_t<tag_invoke_member>,
              Cpo,
              Target,
              ArgN...>)
          -> unifex::tag_invoke_result_t<
              unifex::tag_t<tag_invoke_member>,
              Cpo,
              Target,
              ArgN...> {
    return tag_invoke_member(Cpo{}, (Target &&) t, (ArgN &&) argn...);
  }
};

template <typename Derived, typename... CpoN>
struct tag_invoke_member_base {
  struct type : _tag_invoke_member_base<Derived, CpoN>... {};
};

// struct D : tag_invoke_member_base_t<D, CpoN...> {..}; will
// create a tag_invoke customization for each CPO that will forward to
// tag_invoke_member()
// CPOs customize tag_invoke_member to call a named member of Target.
// this allows
// struct asender : tag_invoke_member_base_t<asender, tag_t<connect>> {
//   template<typename R>
//   op connect(R) {..} // this customizes the connect CPO
// };
template <typename Derived, typename... CpoN>
using tag_invoke_member_base_t =
    typename tag_invoke_member_base<Derived, CpoN...>::type;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
