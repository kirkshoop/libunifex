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

#include <unifex/detail/unifex_fwd.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/unstoppable_token.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _get_stop_source {
  inline const struct _fn {
    static constexpr unstoppable_source default_source{};
    template (typename T)
      (requires (!tag_invocable<_fn, const T&>))
    constexpr auto operator()(const T&) const noexcept
        -> unstoppable_source& {
      return const_cast<unstoppable_source&>(default_source);
    }

    template (typename T)
      (requires tag_invocable<_fn, const T&>)
    constexpr auto operator()(const T& object) const noexcept
        -> tag_invoke_result_t<_fn, const T&> {
      static_assert(
          is_nothrow_tag_invocable_v<_fn, const T&>,
          "get_stop_source() customisations must be declared noexcept");
      return tag_invoke(_fn{}, object);
    }
  } get_stop_source{};
} // namespace _get_stop_source

using _get_stop_source::get_stop_source;

template <typename T>
using get_stop_source_result_t =
    callable_result_t<decltype(get_stop_source), T>;

template <typename Receiver>
using stop_source_type_t =
    get_stop_source_result_t<Receiver>;

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
