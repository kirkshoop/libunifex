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

#include <unifex/detail/unifex_fwd.hpp>

#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

// - get_storage()      // a type (eg. receiver) can cusutomize this to
//                      // provide an async allocator using get_storage_for
namespace _strg_prv {
struct _fn {
  template <typename Target>
  constexpr auto operator()(const Target& t) const {
    return tag_invoke(*this, t);
  }
};
inline constexpr _fn get_storage{};
}  // namespace _strg_prv
using _strg_prv::get_storage;

// - get_storage_for<T>()      // a type (eg. bounded_storage) can cusutomize
//                             // this to provide an async allocator for the
//                             // provided type.
namespace _strg_for_prv {
template <typename T>
struct _fn {
  constexpr auto operator()() const -> void (*)(T);  // tag_pack
  template <typename Target>
  constexpr auto operator()(const Target& t) const {
    return tag_invoke(*this, t);
  }
};
template <typename T>
inline constexpr _fn<T> get_storage_for{};
template <typename T>
inline constexpr bool is_get_storage_for_v = unifex::instance_of_v<_fn, T>;
}  // namespace _strg_for_prv
using _strg_for_prv::get_storage_for;
using _strg_for_prv::is_get_storage_for_v;

// - construct() -> alloc<ref> // returns a sender that will complete with a
//                             // 'ref' to the constructed object
namespace _cnstrct {
struct _fn {
  template <typename Target, typename... ArgN>
  constexpr auto operator()(Target& t, ArgN&&... argn) const {
    return tag_invoke(*this, t, (ArgN &&) argn...);
  }
};
template <typename Target, typename... ArgN>
constexpr auto tag_invoke(
    const unifex::tag_t<tag_invoke_member>&,
    const _fn&,
    Target&& t,
    ArgN&&... argn) noexcept(noexcept(t.construct((ArgN &&) argn...)))
    -> decltype(t.construct((ArgN &&) argn...)) {
  return t.construct((ArgN &&) argn...);
}
inline constexpr _fn construct{};
}  // namespace _cnstrct
using _cnstrct::construct;

// - destruct(ref) -> dalloc   // returns a sender that will complete after the
//                             // storage has been deallocated (and perhaps
//                             // after completing a pending construct())
namespace _dstrct {
struct _fn {
  template <typename Target, typename... ArgN>
  constexpr auto operator()(Target& t, ArgN&&... argn) const {
    return tag_invoke(*this, t, (ArgN &&) argn...);
  }
};
template <typename Target, typename... ArgN>
constexpr auto tag_invoke(
    const unifex::tag_t<tag_invoke_member>&,
    const _fn&,
    Target&& t,
    ArgN&&... argn) noexcept(noexcept(t.destruct((ArgN &&) argn...)))
    -> decltype(t.destruct((ArgN &&) argn...)) {
  return t.destruct((ArgN &&) argn...);
}
inline constexpr _fn destruct{};
}  // namespace _dstrct
using _dstrct::destruct;

namespace _strg {
template <typename Tag>
struct _tag_pack {
  using type = std::remove_reference_t<
      std::remove_pointer_t<decltype(std::declval<Tag>()())>>;
};

// utility to provide access to a pack of template args to a CPO
// the pack is delivered as a 'function' type (eg. void(Tn...))
template(typename Tag)                                            //
    (requires std::is_function_v<typename _tag_pack<Tag>::type>)  //
    using tag_pack_t = typename _tag_pack<Tag>::type;

}  // namespace _strg

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
