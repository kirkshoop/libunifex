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

// reduce_each() - a sequence algorithm
//
// takes a Value that is the initial state of the accumulator
// the returned single sender will produce the accumulated Value
//
// takes an AccumulatorFactory that
//
// takes the current accumulated value
// takes a single sender of the next value-pack in the sequence
//
// returns a single sender that produces the new Value of the accumulator
//
// fixed overhead and fully-typed and supports lock-step
// can be made concurrent
//
//

#include <unifex/detail/prologue.hpp>
namespace unifex {
namespace _rdc_cpo {
inline constexpr struct reduce_fn {
  template <
      typename SuccessorReceiver,
      typename Value,
      typename AccumulatorFactory>
  struct succ_rcvr {
    struct type;
  };
  template <
      typename SuccessorReceiver,
      typename Value,
      typename AccumulatorFactory>
  struct state;
  template <
      typename Predecessor,
      typename SuccessorReceiver,
      typename Value,
      typename AccumulatorFactory>
  struct op {
    struct type;
  };
  template <typename Predecessor, typename Value, typename AccumulatorFactory>
  struct sender {
    struct type;
  };
  template <typename Predecessor, typename Value, typename AccumulatorFactory>
  auto operator()(Predecessor s, Value v, AccumulatorFactory af) const ->
      typename sender<Predecessor, Value, AccumulatorFactory>::type {
    return {std::move(s), std::move(v), std::move(af)};
  }
  template <typename Value, typename AccumulatorFactory>
  constexpr auto operator()(Value&& v, AccumulatorFactory&& af) const
      noexcept(is_nothrow_callable_v<
               tag_t<bind_back>,
               reduce_fn,
               Value,
               AccumulatorFactory>)
          -> bind_back_result_t<reduce_fn, Value, AccumulatorFactory> {
    return bind_back(*this, (Value &&) v, (AccumulatorFactory &&) af);
  }
} reduce_each;
template <
    typename SuccessorReceiver,
    typename Value,
    typename AccumulatorFactory>
struct reduce_fn::succ_rcvr<SuccessorReceiver, Value, AccumulatorFactory>::
    type {
  friend auto tag_invoke(unifex::tag_t<unifex::set_value>, type&& self) {
    // end sequence with reduced Value
    return unifex::set_value(
        std::move(self.op_->successorReceiver_), std::move(self.op_->v_));
  }
  template <typename Error>
  friend auto tag_invoke(
      unifex::tag_t<unifex::set_error>, type&& self, Error&& error) noexcept {
    auto op = self.op_;
    // end sequence with the error
    return unifex::set_error(
        std::move(op->successorReceiver_), (Error &&) error);
  }
  friend auto
  tag_invoke(unifex::tag_t<unifex::set_done>, type&& self) noexcept {
    auto op = self.op_;
    // end sequence with the cancellation
    return unifex::set_done(std::move(op->successorReceiver_));
  }
  template(typename CPO, typename R)               //
      (requires                                    //
       (unifex::is_receiver_query_cpo_v<CPO>) AND  //
       (unifex::same_as<R, type>) AND              //
       (unifex::is_callable_v<
           CPO,
           const SuccessorReceiver&>))  //
      friend auto tag_invoke(CPO cpo, const R& r) noexcept(
          unifex::is_nothrow_callable_v<CPO, const SuccessorReceiver&>)
          -> unifex::callable_result_t<CPO, const SuccessorReceiver&> {
    // queries from a factory sender not satisfied in the
    // FactorySender expression are forwarded to the sequence
    // receiver
    return static_cast<CPO&&>(cpo)(r.op_->successorReceiver_);
  }
  template <typename Func>
  friend void tag_invoke(
      unifex::tag_t<unifex::visit_continuations>, const type& r, Func&& func) {
    std::invoke(func, std::as_const(r.get_receiver()));
  }
  state<SuccessorReceiver, Value, AccumulatorFactory>* op_;
};
template <
    typename SuccessorReceiver,
    typename Value,
    typename AccumulatorFactory>
struct reduce_fn::state {
  SuccessorReceiver successorReceiver_;
  Value v_;
  AccumulatorFactory af_;
};
template <
    typename Predecessor,
    typename SuccessorReceiver,
    typename Value,
    typename AccumulatorFactory>
struct reduce_fn::
    op<Predecessor, SuccessorReceiver, Value, AccumulatorFactory>::type {
  using succ_rcvr_t =
      typename succ_rcvr<SuccessorReceiver, Value, AccumulatorFactory>::type;
  using state_t = state<SuccessorReceiver, Value, AccumulatorFactory>;
  struct update {
    template <typename ItemSender>
    auto operator()(ItemSender&& itemSender) {
      return state_->af_(
                 std::as_const(state_->v_), (ItemSender &&) itemSender) |  //
          then([state_ = this->state_](Value newValue) {
               state_->v_ = std::move(newValue);
             });
    }
    state_t* state_;
  };
  // state breaks the type recursion that causes op to be an incomplete type
  // when af_ and v_ are accessed by update
  using pred_op_t = sequence_connect_result_t<Predecessor, succ_rcvr_t, update>;
  state_t state_;
  pred_op_t predOp_;

  template <
      typename Predecessor2,
      typename SuccessorReceiver2,
      typename Value2,
      typename AccumulatorFactory2>
  type(
      Predecessor2&& predecessor,
      SuccessorReceiver2&& successorReceiver,
      Value2&& v,
      AccumulatorFactory2&& af)
    : state_(
          {(SuccessorReceiver2 &&) successorReceiver,
           (Value2 &&) v,
           (AccumulatorFactory2 &&) af})
    , predOp_(sequence_connect(
          (Predecessor2 &&) predecessor,
          succ_rcvr_t{&state_},
          update{&state_})) {}

  friend auto tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    return unifex::start(self.predOp_);
  }
};
template <typename Predecessor, typename Value, typename AccumulatorFactory>
struct reduce_fn::sender<Predecessor, Value, AccumulatorFactory>::type {
  Predecessor predecessor_;
  Value v_;
  AccumulatorFactory af_;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<Value>>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Predecessor, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = sender_traits<Predecessor>::sends_done;

  template <typename Receiver>
  friend typename op<Predecessor, Receiver, Value, AccumulatorFactory>::type
  tag_invoke(
      unifex::tag_t<unifex::connect>, const type& self, Receiver&& receiver) {
    return {self.predecessor_, (Receiver &&) receiver, self.v_, self.af_};
  }
};
}  // namespace _rdc_cpo
using _rdc_cpo::reduce_each;
}  // namespace unifex
#include <unifex/detail/epilogue.hpp>
