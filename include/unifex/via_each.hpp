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

#include <unifex/scheduler_concepts.hpp>
#include <unifex/sequence_concepts.hpp>
#include <unifex/via.hpp>

// via_each() - a sequence algorithm
//
// fixed overhead and fully-typed and supports lock-step
//
//

#include <unifex/detail/prologue.hpp>
#include "scheduler_concepts.hpp"
namespace unifex {
namespace _via_cpo {
inline constexpr struct via_fn {
  template <typename SenderFactory, typename Scheduler>
  struct state;
  template <
      typename Predecessor,
      typename SuccessorReceiver,
      typename SenderFactory,
      typename Scheduler>
  struct op {
    struct type;
  };
  template <typename Predecessor>
  struct sender {
    struct type;
  };
  template <typename Predecessor>
  auto operator()(Predecessor&& s) const ->
      typename sender<remove_cvref_t<Predecessor>>::type {
    return {(Predecessor &&) s};
  }
  constexpr auto operator()() const
      noexcept(is_nothrow_callable_v<tag_t<bind_back>, via_fn>)
          -> bind_back_result_t<via_fn> {
    return bind_back(*this);
  }
} via_each;
template <typename SenderFactory, typename Scheduler>
struct via_fn::state {
  ~state() {  //
    [[maybe_unused]] auto sf = std::move(sf_);
    [[maybe_unused]] auto sched = std::move(sched_);
  }
  SenderFactory sf_;
  Scheduler sched_;
};
template <
    typename Predecessor,
    typename SuccessorReceiver,
    typename SenderFactory,
    typename Scheduler>
struct via_fn::op<Predecessor, SuccessorReceiver, SenderFactory, Scheduler>::
    type {
  using state_t =
      state<remove_cvref_t<SenderFactory>, remove_cvref_t<Scheduler>>;
  struct via_each {
    ~via_each() {  //
      state_ = nullptr;
    }
    template <typename ItemSender>
    auto operator()(ItemSender&& itemSender) {
      return state_->sf_(via(state_->sched_, (ItemSender &&) itemSender));
    }
    state_t* state_;
  };
  // state breaks the type recursion that causes op to be an incomplete type
  // when discard_ is accessed by via_each
  using pred_op_t =
      sequence_connect_result_t<Predecessor, SuccessorReceiver, via_each>;
  state_t state_;
  pred_op_t predOp_;
  type(const type&) = delete;
  type(type&&) = delete;
  template <
      typename Predecessor2,
      typename SuccessorReceiver2,
      typename SenderFactory2,
      typename Scheduler2>
  type(
      Predecessor2&& predecessor,
      SuccessorReceiver2&& successorReceiver,
      SenderFactory2&& sf,
      Scheduler2&& sched)
    : state_({(SenderFactory2 &&) sf, (Scheduler2 &&) sched})
    , predOp_(unifex::sequence_connect(
          (Predecessor2 &&) predecessor,
          (SuccessorReceiver2 &&) successorReceiver,
          via_each{&state_})) {}

  friend auto tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    return unifex::start(self.predOp_);
  }
};
template <typename Predecessor>
struct via_fn::sender<Predecessor>::type {
  Predecessor predecessor_;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = sender_value_types_t<Predecessor, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Predecessor, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = sender_traits<Predecessor>::sends_done;

  template <typename Sender, typename Scheduler>
  static auto pipeVia(Sender&& s, Scheduler&& sched) {
    return via(sched, (Sender &&) s);
  }
  template <typename Sender, typename Scheduler>
  using via_sender_t =
      decltype(pipeVia(std::declval<Sender>(), std::declval<Scheduler>()));

  template <typename Sender, typename Receiver, typename SenderFactory>
  using op_t = typename op<
      via_sender_t<Sender, get_scheduler_result_t<Receiver>>,
      Receiver,
      SenderFactory,
      get_scheduler_result_t<Receiver>>::type;

  template(typename T, typename Receiver, typename SenderFactory)     //
      (requires                                                       //
       (same_as<remove_cvref_t<T>, type>))                            //
      friend op_t<member_t<T, Predecessor>, Receiver, SenderFactory>  //
      tag_invoke(
          unifex::tag_t<unifex::sequence_connect>,
          T&& self,
          Receiver&& receiver,
          SenderFactory&& sf)  //
      noexcept(
          is_nothrow_callable_v<
              tag_t<unifex::sequence_connect>,
              via_sender_t<
                  member_t<T, Predecessor>,
                  get_scheduler_result_t<Receiver>>,
              Receiver,
              typename op_t<member_t<T, Predecessor>, Receiver, SenderFactory>::
                  via_each>) {
    auto sched = get_scheduler(receiver);
    return op_t<member_t<T, Predecessor>, Receiver, SenderFactory>(
        via(sched, static_cast<T&&>(self).predecessor_),
        (Receiver &&) receiver,
        (SenderFactory &&) sf,
        sched);
  }
};
}  // namespace _via_cpo
using _via_cpo::via_each;
}  // namespace unifex
#include <unifex/detail/epilogue.hpp>
