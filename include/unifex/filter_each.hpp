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
#include <unifex/manual_lifetime_union.hpp>
#include <unifex/just.hpp>
#include <unifex/let_value.hpp>

// filter_each() - a sequence algorithm
//
// takes a Predicate
//
// The Predicate takes the value_types of the predecessor 
// and returns a bool
//
// returns true if the value-pack should be transmitted 
// to the successor
//
// returns false if the value-pack should be ignored
//
// fixed overhead and fully-typed and supports lock-step
//
//

#include <unifex/detail/prologue.hpp>
namespace unifex {
namespace _flt_cpo {
inline constexpr struct filter_fn {
  template<typename ItemOp, typename NoneOp>
  struct alt_op {
    struct type;
  };
  template<typename ItemSender, typename NoneSender>
  struct alt_sender {
    struct type;
  };
  template<typename SenderFactory, typename Predicate>
  struct state;
  template<typename Predecessor, typename SuccessorReceiver, typename SenderFactory, typename Predicate>
  struct op {
    struct type;
  };
  template<typename Predecessor, typename Predicate>
  struct sender {
    struct type;
  };
  template <typename Predecessor, typename Predicate>
  auto operator()(Predecessor s, Predicate keep) const 
    -> typename sender<Predecessor, Predicate>::type {
    return {std::move(s), std::move(keep)};
  }
  template <typename Predicate>
  constexpr auto operator()(Predicate&& keep) const
    noexcept(is_nothrow_callable_v<
        tag_t<bind_back>, filter_fn, Predicate>)
    -> bind_back_result_t<filter_fn, Predicate> {
    return bind_back(*this, (Predicate&&)keep);
  }
} filter_each;
template<typename ItemOp, typename NoneOp>
struct filter_fn::alt_op<ItemOp, NoneOp>::type {
  bool started_;
  bool filtered_;
  manual_lifetime_union<ItemOp, NoneOp> op_;

  ~type() {
    if (!started_) {
      if (filtered_) {
        unifex::deactivate_union_member<NoneOp>(op_);
      } else {
        unifex::deactivate_union_member<ItemOp>(op_);
      }
    }
  }

  template<typename Sender, typename Receiver>
  type(Sender&& sender, Receiver&& receiver) 
    : started_(false)
    , filtered_(same_as<connect_result_t<Sender, Receiver>, NoneOp>) {
    using op_t = connect_result_t<Sender, Receiver>;
    unifex::activate_union_member_with<op_t>(op_, [&] {
      return unifex::connect(
          (Sender &&) sender, (Receiver&&)receiver);
    });
  }

  friend void tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    self.started_ = true;
    if (self.filtered_) {
      unifex::start(self.op_.template get<NoneOp>());
    } else {
      unifex::start(self.op_.template get<ItemOp>());
    }
  }
};
template<typename ItemSender, typename NoneSender>
struct filter_fn::alt_sender<ItemSender, NoneSender>::type {
  bool filtered_;
  manual_lifetime_union<ItemSender, NoneSender> sender_;
  
  ~type() {
    if (filtered_) {
      unifex::deactivate_union_member<NoneSender>(sender_);
    } else {
      unifex::deactivate_union_member<ItemSender>(sender_);
    }
  }
  template<typename Sender>
  explicit type(Sender&& sender) : filtered_(same_as<remove_cvref_t<Sender>, NoneSender>) {
    unifex::activate_union_member_with<Sender>(sender_, [&] {
      return (Sender &&) sender;
    });
  }

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = sender_value_types_t<ItemSender, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = sender_error_types_t<ItemSender, Variant>;

  static constexpr bool sends_done = sender_traits<ItemSender>::sends_done;

  template<typename Receiver>
  friend typename alt_op<connect_result_t<ItemSender, Receiver>, connect_result_t<NoneSender, Receiver>>::type 
  tag_invoke(unifex::tag_t<unifex::connect>, const type& self, Receiver&& receiver) {
    if (self.filtered_) {
      return {self.sender_.template get<NoneSender>(), (Receiver&&)receiver};
    } else {
      return {self.sender_.template get<ItemSender>(), (Receiver&&)receiver};
    }
  }
};
template<typename SenderFactory, typename Predicate>
struct filter_fn::state {
  SenderFactory sf_;
  Predicate keep_;
};
template<typename Predecessor, typename SuccessorReceiver, typename SenderFactory, typename Predicate>
struct filter_fn::op<Predecessor, SuccessorReceiver, SenderFactory, Predicate>::type {
  using state_t = state<SenderFactory, Predicate>;
  struct filter_each {
    template<typename ItemSender>
    auto operator()(ItemSender&& itemSender) {
        auto item = [state = this->state_](auto&&... vn){return state->sf_(just((decltype(vn)&&)vn...));};
        auto none = []{return just();};
        return (ItemSender&&)itemSender | let_value([none, item, state = this->state_](auto&&... vn) {
          using alt_sender_t = typename alt_sender<decltype(item((decltype(vn)&&)vn...)), decltype(none())>::type;
          if (state->keep_(std::as_const(vn)...)) {
            return alt_sender_t{item((decltype(vn)&&)vn...)};
          }
          return alt_sender_t{none()};
        });
    }
    state_t* state_;
  };
  // state breaks the type recursion that causes op to be an incomplete type 
  // when discard_ is accessed by filter_each
  using pred_op_t = sequence_connect_result_t<Predecessor, SuccessorReceiver, filter_each>;
  state_t state_;
  pred_op_t predOp_;

  template<typename Predecessor2, typename SuccessorReceiver2, typename SenderFactory2, typename Predicate2>
  type(Predecessor2&& predecessor,
    SuccessorReceiver2&& successorReceiver,
    SenderFactory2&& sf,
    Predicate2&& keep) 
    : state_({(SenderFactory2&&)sf, (Predicate2&&)keep})
    , predOp_(sequence_connect(
      (Predecessor2&&) predecessor,
      (SuccessorReceiver2&&)successorReceiver,
      filter_each{&state_})) {
  }

  friend void tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    unifex::start(self.predOp_);
  }
};
template<typename Predecessor, typename Predicate>
struct filter_fn::sender<Predecessor, Predicate>::type {

  Predecessor predecessor_;
  Predicate keep_;

  template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
  using value_types = sender_value_types_t<Predecessor, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = sender_error_types_t<Predecessor, Variant>;

  static constexpr bool sends_done = sender_traits<Predecessor>::sends_done;

  template<typename Receiver, typename SenderFactory>
  friend typename op<Predecessor, Receiver, SenderFactory, Predicate>::type 
  tag_invoke(unifex::tag_t<unifex::sequence_connect>, const type& self, Receiver&& receiver, SenderFactory&& sf) {
    return {self.predecessor_, (Receiver&&)receiver, (SenderFactory&&)sf, self.keep_};
  }
};
} // namespace _flt_cpo
using _flt_cpo::filter_each;
} // namespace unifex
#include <unifex/detail/epilogue.hpp>
