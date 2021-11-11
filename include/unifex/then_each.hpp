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

#include <unifex/sequence_concepts.hpp>
#include <unifex/then.hpp>

// then_each() - a sequence algorithm
//
// takes a Function
//
// The Function takes the value_types of the predecessor
// and returns the new value
//
// returns the new value (returning a pack is not
// supported in the language). A pack can be returned as a tuple.
// The tuple will not be silently unpacked - the tuple will be
// passed to set_value unchanged
//
// fixed overhead and fully-typed and supports lock-step
//
//

#include <unifex/detail/prologue.hpp>
namespace unifex {
namespace _thn_cpo {
namespace detail {
template <typename Result, typename = void>
struct result_overload {
  using type = type_list<Result>;
};
template <typename Result>
struct result_overload<Result, std::enable_if_t<std::is_void_v<Result>>> {
  using type = type_list<>;
};
}  // namespace detail
inline constexpr struct then_fn {
  template <typename SenderFactory, typename Transform>
  struct state;
  template <
      typename Predecessor,
      typename SuccessorReceiver,
      typename SenderFactory,
      typename Transform>
  struct op {
    struct type;
  };
  template <typename Predecessor, typename Transform>
  struct sender {
    struct type;
  };
  template <typename Predecessor, typename Transform>
  auto operator()(Predecessor s, Transform trns) const ->
      typename sender<Predecessor, Transform>::type {
    return {std::move(s), std::move(trns)};
  }
  template <typename Transform>
  constexpr auto operator()(Transform&& trns) const
      noexcept(is_nothrow_callable_v<tag_t<bind_back>, then_fn, Transform>)
          -> bind_back_result_t<then_fn, Transform> {
    return bind_back(*this, (Transform &&) trns);
  }
} then_each;
template <typename SenderFactory, typename Transform>
struct then_fn::state {
  SenderFactory sf_;
  Transform trns_;
};
template <
    typename Predecessor,
    typename SuccessorReceiver,
    typename SenderFactory,
    typename Transform>
struct then_fn::op<Predecessor, SuccessorReceiver, SenderFactory, Transform>::
    type {
  using state_t = state<SenderFactory, Transform>;
  struct then_each {
    template <typename ItemSender>
    auto operator()(ItemSender&& itemSender) {
      return state_->sf_((ItemSender &&) itemSender | then(state_->trns_));
    }
    state_t* state_;
  };
  // state breaks the type recursion that causes op to be an incomplete type
  // when discard_ is accessed by then_each
  using pred_op_t =
      sequence_connect_result_t<Predecessor, SuccessorReceiver, then_each>;
  state_t state_;
  pred_op_t predOp_;

  template <
      typename Predecessor2,
      typename SuccessorReceiver2,
      typename SenderFactory2,
      typename Transform2>
  type(
      Predecessor2&& predecessor,
      SuccessorReceiver2&& successorReceiver,
      SenderFactory2&& sf,
      Transform2&& trns)
    : state_({(SenderFactory2 &&) sf, (Transform2 &&) trns})
    , predOp_(sequence_connect(
          (Predecessor2 &&) predecessor,
          (SuccessorReceiver2 &&) successorReceiver,
          then_each{&state_})) {}

  friend auto tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    return unifex::start(self.predOp_);
  }
};
template <typename Predecessor, typename Transform>
struct then_fn::sender<Predecessor, Transform>::type {
  Predecessor predecessor_;
  Transform trns_;

  // This helper transforms an argument list into either
  // - type_list<type_list<Result>> - if Result is non-void, or
  // - type_list<type_list<>>       - if Result is void
  template <typename... Args>
  using result = type_list<typename detail::result_overload<
      callable_result_t<Transform, Args...>>::type>;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = type_list_nested_apply_t<
      sender_value_types_t<Predecessor, concat_type_lists_unique_t, result>,
      Variant,
      Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Predecessor, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = sender_traits<Predecessor>::sends_done;

  template <typename Receiver, typename SenderFactory>
  friend typename op<Predecessor, Receiver, SenderFactory, Transform>::type
  tag_invoke(
      unifex::tag_t<unifex::sequence_connect>,
      const type& self,
      Receiver&& receiver,
      SenderFactory&& sf) {
    return {
        self.predecessor_,
        (Receiver &&) receiver,
        (SenderFactory &&) sf,
        self.trns_};
  }
};
}  // namespace _thn_cpo
using _thn_cpo::then_each;
}  // namespace unifex
#include <unifex/detail/epilogue.hpp>
