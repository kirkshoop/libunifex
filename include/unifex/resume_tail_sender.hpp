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
#include <optional>
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
    auto op = unifex::connect(std::move(c), r);
    return _resume_until_nullable(unifex::start(op), std::move(r));
  }
}

template(typename C, typename Receiver)  //
    (requires                            //
     (!std::is_void_v<C>) &&             //
     (tail_receiver<Receiver>)&&         //
     (_tail_sender<C>))                  //
    auto _start_until_nullable(C c, Receiver r) {
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
    return _start_until_nullable(unifex::start(op), std::move(r));
  }
}

template <typename Next, typename C, typename Receiver, typename... Prev>
auto _start_next(Next next, Receiver r) {
  if constexpr (one_of<Next, C, Prev...>) {
    static_assert(
        (nullable_tail_sender_to<C, Receiver> ||
         (nullable_tail_sender_to<Prev, Receiver> || ...)),
        "At least one tail_sender in a cycle must be nullable to avoid "
        "entering an infinite loop");
    return _start_until_nullable(next, std::move(r));
  } else {
    using result_type =
        decltype(_start_sequential(next, r, type_list<C, Prev...>{}));
    if constexpr (same_as<result_type, Next>) {
      // Let the loop in resume_tail_sender() handle checking the boolean.
      return next;
    } else {
      return _start_sequential(next, std::move(r), type_list<C, Prev...>{});
    }
  }
}

template(typename C, typename Receiver, typename... Prev)  //
    (requires                                              //
     (!std::is_void_v<C>) &&                               //
     (tail_receiver<Receiver>)&&                           //
     (_tail_sender<C>))                                    //
    auto _start_sequential(C c, Receiver r, type_list<Prev...>) {
  static_assert(
      _tail_sender<C>, "_start_sequential: must be called with a tail_sender");
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
    using result_type = decltype(_start_next<next_t, C, Receiver, Prev...>(
        std::declval<next_t>(), r));
    if constexpr (std::is_void_v<next_t>) {
      // restrict scope of op
      {
        auto op = unifex::connect(std::move(c), std::move(r));
        unifex::start(op);
      }
      return null_tail_sender{};
    } else if constexpr (same_as<result_type, next_t>) {
      auto op = unifex::connect(std::move(c), std::move(r));
      return unifex::start(op);
    } else if constexpr (nullable_tail_sender_to<C, Receiver>) {
      auto op = unifex::connect(std::move(c), r);
      using result_type = variant_tail_sender<
          null_tail_sender,
          decltype(_start_next<next_t, C, Receiver, Prev...>(
              unifex::start(op), r))>;
      if (!op) {
        return result_type{null_tail_sender{}};
      }
      return result_type{
          _start_next<next_t, C, Receiver, Prev...>(unifex::start(op), r)};
    } else {
      auto op = unifex::connect(std::move(c), r);
      return _start_next<next_t, C, Receiver, Prev...>(unifex::start(op), r);
    }
  }
}

template(typename C, typename Receiver)  //
    (requires                            //
     (!std::is_void_v<C>) &&             //
     (tail_receiver<Receiver>)&&         //
     (tail_sender<C>))                   //
    auto _start_sequential(C c, Receiver r) {
  return _start_sequential(c, r, type_list<>{});
}

template(typename C, typename Receiver = null_tail_receiver)  //
    (requires                                                 //
     (!std::is_void_v<C>) &&                                  //
     (receiver<Receiver>)&&                                   //
     (tail_sender_to<C, Receiver>))                           //
    void resume_tail_sender(C c, Receiver r = Receiver{}) {
  static_assert(
      nullable_tail_sender_to<decltype(_start_sequential(c, r)), Receiver>,
      "resume_tail_sender: _start_sequential must return a "
      "nullable_tail_sender");
  auto c2 = _start_sequential(c, r);
  for (;;) {
    auto op = unifex::connect(c2, r);
    if (!op) {
      break;
    }
    if constexpr (_terminal_tail_sender_to<decltype(c2), Receiver>) {
      unifex::start(op);
      break;
    } else {
      c2 = _start_sequential(unifex::start(op), r, type_list<>{});
    }
  }
}

// start_one and result_or_null have to cooperate
// to ensure that the result is not overwritten by a
// a terminating tail_sender as this would terminate
// before completing the remaining tail_sender(s)
struct _start_one_null_tail_sender : null_tail_sender {};
template <typename TailSender, typename Receiver>
auto _start_one(TailSender s, Receiver r, size_t& remaining) noexcept {
  using start_result_t = next_tail_sender_to_t<TailSender, Receiver>;
  auto op = unifex::connect(s, r);
  if constexpr (std::is_void_v<start_result_t>) {
    if constexpr (nullable_tail_sender_to<TailSender, Receiver>) {
      if (!!op) {
        unifex::start(op);
      }
    } else {
      unifex::start(op);
    }
    --remaining;
    return _start_one_null_tail_sender{};
  } else if constexpr (same_as<null_tail_sender, start_result_t>) {
    --remaining;
    unifex::start(op);
    return _start_one_null_tail_sender{};
  } else {
    using one_result_type =
        variant_tail_sender<start_result_t, _start_one_null_tail_sender>;
    if constexpr (nullable_tail_sender_to<TailSender, Receiver>) {
      if (!op) {
        --remaining;
        return one_result_type{_start_one_null_tail_sender{}};
      }
    }
    if constexpr (instance_of_v<_variant_tail_sender, start_result_t>) {
      auto r = unifex::start(op);
      if (r.template contains<null_tail_sender>()) {
        --remaining;
        return one_result_type{_start_one_null_tail_sender{}};
      }
      return one_result_type{r};
    } else {
      return one_result_type{unifex::start(op)};
    }
  }
};

template <typename Result, typename One>
auto _resolve_result(Result& result, One one) noexcept {
  if constexpr (!same_as<One, _start_one_null_tail_sender>) {
    if (!one.template contains<_start_one_null_tail_sender>()) {
      // update the result with a new valid tail_sender
      result = one;
    }
  }
};

template(typename Receiver)      //
    (requires                    //
     (tail_receiver<Receiver>))  //
    inline null_tail_sender
    resume_tail_senders_until_one_remaining(Receiver) noexcept {
  return {};
}

template(typename Receiver, typename C)  //
    (requires                            //
     (tail_receiver<Receiver>) AND       //
     (_tail_sender<C>))                  //
    C resume_tail_senders_until_one_remaining(Receiver, C c) noexcept {
  return c;
}

template(typename... Cs, std::size_t... Is, typename Receiver)  //
    (requires                                                   //
     (tail_receiver<Receiver>) AND                              //
     (all_true<_tail_sender<Cs>...>))                           //
    UNIFEX_ALWAYS_INLINE auto _resume_tail_senders_until_one_remaining(
        std::index_sequence<Is...>, Receiver r, Cs... cs) noexcept {
  std::size_t remaining = sizeof...(cs);

  using result_type = variant_tail_sender<decltype(_start_sequential(
      _start_one(cs, r, remaining), r))...>;
  result_type result;

  auto cs2_tuple =
      std::make_tuple(_start_sequential(_start_one(cs, r, remaining), r)...);

  while (true) {
    remaining = sizeof...(cs);
    ((remaining > 1
          ? (void)(_resolve_result(
                result,
                std::get<Is>(cs2_tuple)  //
                = _start_sequential(
                    _start_one(std::get<Is>(cs2_tuple), r, remaining), r)))
          : (void)(result = std::get<Is>(cs2_tuple))),
     ...);

    if (remaining <= 1) {
      return result;
    }
  }
}

template(typename Receiver, typename... Cs)        //
    (requires                                      //
     (tail_receiver<Receiver>) AND                 //
     (all_true<tail_sender_to<Cs, Receiver>...>))  //
    auto resume_tail_senders_until_one_remaining(
        Receiver r, Cs... cs) noexcept {
  return _resume_tail_senders_until_one_remaining(
      std::index_sequence_for<Cs...>{}, r, cs...);
}
template(typename C0, typename... Cs)  //
    (requires                          //
     (tail_sender<C0>) AND             //
     (all_true<tail_sender<Cs>...>))   //
    auto resume_tail_senders_until_one_remaining(C0 c0, Cs... cs) noexcept {
  return _resume_tail_senders_until_one_remaining(
      std::index_sequence_for<C0, Cs...>{}, null_tail_receiver{}, c0, cs...);
}
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
