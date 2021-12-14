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
#include <unifex/continuations.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sequence_concepts.hpp>
#include <unifex/variant_tail_sender.hpp>

#include <chrono>

// interval() - a sequence algorithm
//
// when the interval op start() is called, start() is called on a tickOp
// obtained from schedule_at using the contextual scheduler.
//
// when the tick_receiver set_value() is called, start() is called on a
// factoryOp obtained from the SenderFactory
//
// when the factoryOp start() is called it calls set_value(tick) to deliver the
// tick item to the expression in the SenderFactory function
//
// when the factory_receiver set_value() is called, start() is called on a new
// tickOp
//
// fixed overhead and fully-typed and supports lock-step
//
// clean separation of scopes for nested expressions on items
//
// errors and cancellation from the expression for an item are proxied to the
// sequence scope which stops the whole sequence by emiting them to the sequence
// receiver.
//

#include <unifex/detail/prologue.hpp>
namespace unifex {
namespace _inrvl_cpo {
inline constexpr struct interval_fn {
  template <
      typename Clock,
      typename FinalReceiver,
      typename SenderFactory,
      typename Receiver>
  struct factory_op {
    struct type;
  };
  template <typename Clock, typename Receiver, typename SenderFactory>
  struct op {
    using time_point = typename Clock::time_point;
    using duration = typename Clock::duration;
    using scheduler = remove_cvref_t<unifex::callable_result_t<
        unifex::tag_t<unifex::get_scheduler>,
        Receiver>>;
    using tick_sender = remove_cvref_t<unifex::callable_result_t<
        unifex::tag_t<unifex::schedule_at>,
        scheduler&,
        time_point>>;

    struct tail_start;
    struct tail_restart;
    struct tick_receiver;
    struct factory_sender;
    struct factory_receiver;
    struct type;
  };
  template <typename Clock>
  struct sender {
    struct type;
  };
  template <typename TimePoint, typename Duration>
  auto operator()(TimePoint reference, Duration gap) const ->
      typename sender<typename TimePoint::clock>::type {
    return {reference, gap};
  }
} interval;
template <
    typename Clock,
    typename FinalReceiver,
    typename SenderFactory,
    typename Receiver>
struct interval_fn::factory_op<Clock, FinalReceiver, SenderFactory, Receiver>::
    type {
  using interval_t = interval_fn::op<Clock, FinalReceiver, SenderFactory>;
  using interval_op = typename interval_t::type;
  using time_point = typename Clock::time_point;
  using duration = typename Clock::duration;

  interval_op* op_;
  time_point expected_;
  Receiver receiver_;

  template <typename Receiver2>
  type(interval_op* op, time_point expected, Receiver2&& r)
    : op_(op)
    , expected_(expected)
    , receiver_((Receiver2 &&) r) {}

  type(const type&) = delete;
  type(type&&) = delete;
  type& operator=(const type&) = delete;
  type& operator=(type&&) = delete;

  using result_t = variant_tail_sender<
      callable_result_t<tag_t<unifex::set_value>, Receiver, const time_point&>,
      callable_result_t<tag_t<unifex::set_done>, Receiver>,
      callable_result_t<
          tag_t<unifex::set_error>,
          FinalReceiver,
          std::exception_ptr>>;

  friend result_t
  tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    UNIFEX_TRY {
      return result_or_null_tail_sender(
          unifex::set_value,
          std::move(self.receiver_),
          std::as_const(self.expected_));
      // self is considered destructed
    }
    UNIFEX_CATCH(...) {
      auto op = self.op_;
      return result_or_null_tail_sender(
          unifex::set_done, std::move(self.receiver_));
      // self is considered destructed

      // end sequence with the error
      return result_or_null_tail_sender(
          unifex::set_error,
          std::move(op->receiver_),
          std::current_exception());
    }
  }
};

template <typename Clock, typename Receiver, typename SenderFactory>
struct interval_fn::op<Clock, Receiver, SenderFactory>::tail_start {
  using op_t = interval_fn::op<Clock, Receiver, SenderFactory>::type;
  struct type : tail_operation_state_base {
    auto start() noexcept {
      auto op = op_;
      return unifex::start(op->tickOp_);
    }
    void unwind() noexcept {
      auto op = op_;
      result_or_null_tail_sender(unifex::set_done, std::move(op->receiver_))
          .unwind();
    }
    op_t* op_;
  };
  constexpr type operator()() noexcept { return type{{}, op_}; }
  op_t* op_;
};
template <typename Clock, typename Receiver, typename SenderFactory>
struct interval_fn::op<Clock, Receiver, SenderFactory>::tail_restart {
  using op_t = interval_fn::op<Clock, Receiver, SenderFactory>::type;
  struct type : tail_operation_state_base {
    variant_tail_sender<
        callable_result_t<tag_t<unifex::tail>, tail_start>,
        callable_result_t<
            tag_t<unifex::set_error>,
            Receiver,
            std::exception_ptr>>
    start() noexcept {
      auto op = op_;
      op->tick_ += op->gap_;

      using tick_op_t = decltype(op->tickOp_);
      op->tickOp_.~tick_op_t();

      using factory_op_t = decltype(op->factoryOp_);
      op->factoryOp_.~factory_op_t();

      UNIFEX_TRY {
        new (&op->tickOp_) auto(unifex::connect(
            unifex::schedule_at(op->scheduler_, op->tick_), tick_receiver{op}));
        new (&op->factoryOp_) auto(unifex::connect(
            op->sf_(factory_sender{op, op->tick_}), factory_receiver{op}));
      }
      UNIFEX_CATCH(...) {
        return {
            unifex::set_error,
            std::move(op->receiver_),
            std::current_exception()};
      }
      return tail(tail_start{op});
    }
    void unwind() noexcept {
      auto op = op_;
      result_or_null_tail_sender(unifex::set_done, std::move(op->receiver_))
          .unwind();
    }
    op_t* op_;
  };
  constexpr type operator()() noexcept { return type{{}, op_}; }
  op_t* op_;
};
template <typename Clock, typename Receiver, typename SenderFactory>
struct interval_fn::op<Clock, Receiver, SenderFactory>::factory_sender {
  template <typename FactoryReceiver>
  using factory_op_t = typename interval_fn::
      factory_op<Clock, Receiver, SenderFactory, FactoryReceiver>::type;
  using time_point = typename Clock::time_point;
  using duration = typename Clock::duration;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<time_point>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  template <typename Receiver2>
  friend auto tag_invoke(
      unifex::tag_t<unifex::connect>,
      const factory_sender& self,
      Receiver2&& receiver) -> factory_op_t<Receiver2> {
    return {self.op_, self.expected_, (Receiver2 &&) receiver};
  }

  type* op_;
  time_point expected_;
};
template <typename Clock, typename Receiver, typename SenderFactory>
struct interval_fn::op<Clock, Receiver, SenderFactory>::factory_receiver {
  template <typename... VN>
  friend auto tag_invoke(
      unifex::tag_t<unifex::set_value>, factory_receiver&& self, VN&&...) {
    static_assert(
        sizeof...(VN) == 0,
        "interval expects the sender factory to return a sender of void");
    return tail(tail_restart{self.op_});
  }
  template <typename Error>
  friend callable_result_t<tag_t<unifex::set_error>, Receiver, Error>
  tag_invoke(
      unifex::tag_t<unifex::set_error>,
      factory_receiver&& self,
      Error&& error) noexcept {
    auto op = self.op_;
    // end sequence with the error
    return unifex::set_error(std::move(op->receiver_), (Error &&) error);
  }
  friend callable_result_t<tag_t<unifex::set_value>, Receiver> tag_invoke(
      unifex::tag_t<unifex::set_done>, factory_receiver&& self) noexcept {
    auto op = self.op_;
    // end sequence. This sequence can only be ended by error or cancellation.
    // composition is helped by treating cancellation as successful completion
    return unifex::set_value(std::move(op->receiver_));
  }
  template(typename CPO, typename R)                   //
      (requires                                        //
       (unifex::is_receiver_query_cpo_v<CPO>) AND      //
       (unifex::same_as<R, factory_receiver>) AND      //
       (unifex::is_callable_v<CPO, const Receiver&>))  //
      friend auto tag_invoke(CPO cpo, const R& r)      //
      noexcept(unifex::is_nothrow_callable_v<CPO, const Receiver&>)
          -> unifex::callable_result_t<CPO, const Receiver&> {
    // queries from a factory sender not satisfied in the
    // FactorySender expression are forwarded to the sequence
    // receiver
    return static_cast<CPO&&>(cpo)(r.op_->receiver_);
  }
  template <typename Func>
  friend void tag_invoke(
      unifex::tag_t<unifex::visit_continuations>,
      const factory_receiver& r,
      Func&& func) {
    std::invoke(func, std::as_const(r.get_receiver()));
  }
  type* op_;
};
template <typename Clock, typename Receiver, typename SenderFactory>
struct interval_fn::op<Clock, Receiver, SenderFactory>::tick_receiver {
  using factory_op = unifex::connect_result_t<
      remove_cvref_t<unifex::callable_result_t<SenderFactory, factory_sender>>,
      factory_receiver>;
  friend callable_result_t<tag_t<unifex::start>, factory_op&>
  tag_invoke(unifex::tag_t<unifex::set_value>, tick_receiver&& self) {
    auto op = self.op_;
    return unifex::start(op->factoryOp_);
  }
  template <typename Error>
  friend callable_result_t<tag_t<unifex::set_error>, Receiver, Error>
  tag_invoke(
      unifex::tag_t<unifex::set_error>,
      tick_receiver&& self,
      Error&& error) noexcept {
    return unifex::set_error(std::move(self.op_->receiver_), (Error &&) error);
  }
  friend callable_result_t<tag_t<unifex::set_value>, Receiver>
  tag_invoke(unifex::tag_t<unifex::set_done>, tick_receiver&& self) noexcept {
    // end sequence. This sequence can only be ended by error or cancellation.
    // composition is helped by treating cancellation as successful completion
    return unifex::set_value(std::move(self.op_->receiver_));
  }
  template(typename CPO, typename R)                   //
      (requires                                        //
       (unifex::is_receiver_query_cpo_v<CPO>) AND      //
       (unifex::same_as<R, tick_receiver>) AND         //
       (unifex::is_callable_v<CPO, const Receiver&>))  //
      friend auto tag_invoke(CPO cpo, const R& r)      //
      noexcept(unifex::is_nothrow_callable_v<CPO, const Receiver&>)
          -> unifex::callable_result_t<CPO, const Receiver&> {
    return static_cast<CPO&&>(cpo)(r.op_->receiver_);
  }
  template <typename Func>
  friend void tag_invoke(
      unifex::tag_t<unifex::visit_continuations>,
      const tick_receiver& r,
      Func&& func) {
    std::invoke(func, std::as_const(r.get_receiver()));
  }
  type* op_;
};
template <typename Clock, typename Receiver, typename SenderFactory>
struct interval_fn::op<Clock, Receiver, SenderFactory>::type {
  using tick_op = unifex::connect_result_t<tick_sender, tick_receiver>;
  using factory_op = unifex::connect_result_t<
      remove_cvref_t<unifex::callable_result_t<SenderFactory, factory_sender>>,
      factory_receiver>;

  time_point reference_;
  time_point tick_;
  duration gap_;
  Receiver receiver_;
  SenderFactory sf_;
  scheduler scheduler_;
  tick_op tickOp_;
  factory_op factoryOp_;

  template <typename Receiver2, typename SenderFactory2>
  type(
      time_point reference,
      duration gap,
      Receiver2&& r,
      SenderFactory2&& sf,
      scheduler sched)
    : reference_(reference)
    , tick_(reference)
    , gap_(gap)
    , receiver_((Receiver2 &&) r)
    , sf_((SenderFactory &&) sf)
    , scheduler_(sched)
    , tickOp_(unifex::connect(
          unifex::schedule_at(scheduler_, tick_), tick_receiver{this}))
    , factoryOp_(unifex::connect(
          sf_(factory_sender{this, tick_}), factory_receiver{this})) {}

  type(const type&) = delete;
  type(type&&) = delete;
  type& operator=(const type&) = delete;
  type& operator=(type&&) = delete;

  friend auto tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    return unifex::start(self.tickOp_);
  }
};
template <typename Clock>
struct interval_fn::sender<Clock>::type {
  using time_point = typename Clock::time_point;
  using duration = typename Clock::duration;
  time_point reference_;
  duration gap_;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<time_point>>;

  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;

  static constexpr bool sends_done = true;

  template <typename Receiver, typename SenderFactory>
  friend auto tag_invoke(
      unifex::tag_t<unifex::sequence_connect>,
      const type& self,
      Receiver&& receiver,
      SenderFactory&& sf) -> typename op<Clock, Receiver, SenderFactory>::type {
    auto scheduler = unifex::get_scheduler(receiver);
    return {
        self.reference_,
        self.gap_,
        (Receiver &&) receiver,
        (SenderFactory &&) sf,
        std::move(scheduler)};
  }
};
}  // namespace _inrvl_cpo
using _inrvl_cpo::interval;
}  // namespace unifex
#include <unifex/detail/epilogue.hpp>
