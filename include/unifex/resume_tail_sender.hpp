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
#include <unifex/tail_sender_concepts.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/variant_tail_sender.hpp>

#include <algorithm>
#include <exception>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

template(typename C, typename Receiver)  //
    (requires                            //
     (!std::is_void_v<C>) &&             //
     (tail_receiver<Receiver>)&&         //
     (_tail_sender<C>))                  //
    auto _resume_until_nullable(C c, Receiver r) {
  if constexpr (nullable_tail_sender_to<C, Receiver>) {
    return c;
  } else {
    // restrict scope of op
    auto c2 = [&]() {
      auto op = unifex::connect(std::move(c), r);
      return unifex::start(op);
    }();
    return _resume_until_nullable(std::move(c2), std::move(r));
  }
}

template(typename C, typename Receiver)  //
    (requires                            //
     (!std::is_void_v<C>) &&             //
     (tail_receiver<Receiver>)&&         //
     (_tail_sender<C>))                  //
    auto _invoke_until_nullable(C c, Receiver r) {
  if constexpr (nullable_tail_sender_to<C, Receiver>) {
    return c;
  } else if constexpr (_terminal_tail_sender_to<C, Receiver>) {
    // restrict scope of op
    {
      auto op = unifex::connect(std::move(c), std::move(r));
      unifex::start(op);
    }
    return null_tail_sender{};
  } else {
    auto op = unifex::connect(std::move(c), r);
    return _invoke_until_nullable(unifex::start(op), std::move(r));
  }
}

template(typename C, typename Receiver, typename... Prev)  //
    (requires(!std::is_void_v<C>))                         //
    auto _invoke_sequential(C c, Receiver r, type_list<Prev...>) {
  static_assert(
      _tail_sender<C>, "_invoke_sequential: must be called with a tail_sender");
  if constexpr (_terminal_tail_sender_to<C, Receiver>) {
    if constexpr (nullable_tail_sender_to<C, Receiver>) {
      return c;
    } else {
      // restrict scope of op
      {
        auto op = unifex::connect(std::move(c), std::move(r));
        unifex::start(op);
      }
      return null_tail_sender{};
    }
  } else {
    using next_t = next_tail_sender_to_t<C, Receiver>;
    if constexpr (std::is_void_v<next_t>) {
      return null_tail_sender{};
    } else {
      using opt_t = std::optional<next_t>;
      // restrict scope of op
      opt_t next = [&]() -> opt_t {
        auto op = unifex::connect(std::move(c), r);
        if constexpr (nullable_tail_sender_to<C, Receiver>) {
          if (!op) {
            return {std::nullopt};
          }
        }
        return {unifex::start(op)};
      }();

      if constexpr (one_of<next_t, C, Prev...>) {
        static_assert(
            (nullable_tail_sender_to<C, Receiver> ||
             (nullable_tail_sender_to<Prev, Receiver> || ...)),
            "At least one tail_sender in a cycle must be nullable to avoid "
            "entering an infinite loop");
        using result_type = variant_tail_sender<
            null_tail_sender,
            decltype(_invoke_until_nullable(*next, std::move(r)))>;
        if (!next) {
          return result_type{null_tail_sender{}};
        }
        return result_type{_invoke_until_nullable(*next, std::move(r))};
      } else {
        using result_type = variant_tail_sender<
            null_tail_sender,
            next_t,
            decltype(_invoke_sequential(*next, r, type_list<C, Prev...>{}))>;
        if constexpr (nullable_tail_sender_to<C, Receiver>) {
          if (!next) {
            return result_type{null_tail_sender{}};
          }
        }
        if constexpr (same_as<result_type, next_t>) {
          // Let the loop in resume_tail_sender() handle checking the boolean.
          return result_type{*next};
        } else {
          return result_type{
              _invoke_sequential(*next, std::move(r), type_list<C, Prev...>{})};
        }
      }
    }
  }
}

template(typename C, typename Receiver)  //
    (requires                            //
     (!std::is_void_v<C>) &&             //
     (tail_receiver<Receiver>))          //
    auto _invoke_sequential(C c, Receiver r) {
  static_assert(
      _tail_sender<C>, "_invoke_sequential: must be called with a tail_sender");
  return _invoke_sequential(c, r, type_list<>{});
}

template(typename C, typename Receiver = null_tail_receiver)  //
    (requires                                                 //
     (!std::is_void_v<C>) &&                                  //
     (tail_receiver<Receiver>)&&                              //
     (_tail_sender<C>))                                       //
    void resume_tail_sender(C c, Receiver r = Receiver{}) {
  static_assert(
      nullable_tail_sender_to<decltype(_invoke_sequential(c, r)), Receiver>,
      "resume_tail_sender: _invoke_sequential must return a "
      "nullable_tail_sender");
  auto c2 = _invoke_sequential(c, r);
  for (;;) {
    auto op = unifex::connect(c2, r);
    if (!op) {
      break;
    }
    if constexpr (_terminal_tail_sender_to<decltype(c2), Receiver>) {
      unifex::start(op);
      break;
    } else {
      c2 = _invoke_sequential(unifex::start(op), r, type_list<>{});
    }
  }
}
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
