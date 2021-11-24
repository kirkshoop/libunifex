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
#include <unifex/just.hpp>
#include <unifex/let_value.hpp>
#include <unifex/manual_lifetime_union.hpp>
#include <unifex/sequence_concepts.hpp>
#include <unifex/variant_tail_sender.hpp>

#include <unifex/then.hpp>

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
#include "sender_concepts.hpp"
namespace unifex {
namespace _flt_cpo {
inline constexpr struct filter_fn {
  template <typename ItemOp, typename NoneOp>
  struct alt_op {
    struct type;
  };
  template <typename ItemSender, typename NoneSender>
  struct alt_sender {
    struct type;
  };
  template <typename SenderFactory, typename Predicate>
  struct state;
  template <
      typename Predecessor,
      typename SuccessorReceiver,
      typename SenderFactory,
      typename Predicate>
  struct op {
    struct type;
  };
  template <typename Predecessor, typename Predicate>
  struct sender {
    struct type;
  };
  template <typename Predecessor, typename Predicate>
  auto operator()(Predecessor&& s, Predicate&& keep) const ->
      typename sender<remove_cvref_t<Predecessor>, remove_cvref_t<Predicate>>::
          type {
    return {(Predecessor &&) s, (Predicate &&) keep};
  }
  template <typename Predicate>
  constexpr auto operator()(Predicate&& keep) const
      noexcept(is_nothrow_callable_v<tag_t<bind_back>, filter_fn, Predicate>)
          -> bind_back_result_t<filter_fn, Predicate> {
    return bind_back(*this, (Predicate &&) keep);
  }
} filter_each;
template <typename ItemOp, typename NoneOp>
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

  template(typename Sender, typename Receiver)  //
      (requires                                 //
       (one_of<
           remove_cvref_t<connect_result_t<Sender, Receiver>>,
           NoneOp,
           ItemOp>))  //
      type(Sender&& sender, Receiver&& receiver)
    : started_(false)
    , filtered_(
          same_as<remove_cvref_t<connect_result_t<Sender, Receiver>>, NoneOp>) {
    using op_t = remove_cvref_t<connect_result_t<Sender, Receiver>>;
    unifex::activate_union_member_with<op_t>(op_, [&] {
      return unifex::connect((Sender &&) sender, (Receiver &&) receiver);
    });
  }

  friend variant_tail_sender<
      callable_result_t<tag_t<unifex::start>, NoneOp&>,
      callable_result_t<tag_t<unifex::start>, ItemOp&>>
  tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    self.started_ = true;
    if (self.filtered_) {
      return result_or_null_tail_sender(
          unifex::start, self.op_.template get<NoneOp>());
    } else {
      return result_or_null_tail_sender(
          unifex::start, self.op_.template get<ItemOp>());
    }
  }
};
template <typename ItemSender, typename NoneSender>
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
  template(typename Sender)                                       //
      (requires                                                   //
       (!same_as<remove_cvref_t<Sender>, type>) AND               //
       (one_of<remove_cvref_t<Sender>, NoneSender, ItemSender>))  //
      explicit type(Sender&& sender)
    : filtered_(same_as<remove_cvref_t<Sender>, NoneSender>) {
    unifex::activate_union_member_with<remove_cvref_t<Sender>>(
        sender_, [&] { return (Sender &&) sender; });
  }

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = sender_value_types_t<ItemSender, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = sender_error_types_t<ItemSender, Variant>;

  static constexpr bool sends_done = sender_traits<ItemSender>::sends_done;

  template <typename Receiver>
  using alt_op_t = typename alt_op<
      remove_cvref_t<connect_result_t<ItemSender, Receiver>>,
      remove_cvref_t<connect_result_t<NoneSender, Receiver>>>::type;

  template(typename T, typename Receiver)   //
      (requires                             //
       (same_as<remove_cvref_t<T>, type>))  //
      friend alt_op_t<Receiver>             //
      tag_invoke(
          unifex::tag_t<unifex::connect>,
          T&& self,
          Receiver&& receiver)  //
      noexcept((is_nothrow_callable_v<
                tag_t<unifex::connect>,
                member_t<T, NoneSender>,
                Receiver>)&&  //
               (is_nothrow_callable_v<
                   tag_t<unifex::connect>,
                   member_t<T, ItemSender>,
                   Receiver>)) {
    if (self.filtered_) {
      return alt_op_t<Receiver>(
          static_cast<T&&>(self).sender_.template get<NoneSender>(),
          (Receiver &&) receiver);
    } else {
      return alt_op_t<Receiver>(
          static_cast<T&&>(self).sender_.template get<ItemSender>(),
          (Receiver &&) receiver);
    }
  }
};
template <typename SenderFactory, typename Predicate>
struct filter_fn::state {
  SenderFactory sf_;
  Predicate keep_;
};
template <
    typename Predecessor,
    typename SuccessorReceiver,
    typename SenderFactory,
    typename Predicate>
struct filter_fn::op<Predecessor, SuccessorReceiver, SenderFactory, Predicate>::
    type {
  using state_t = state<SenderFactory, Predicate>;
  struct filter_each {
    template <typename ItemSender>
    auto operator()(ItemSender&& itemSender) {
      [[maybe_unused]] auto item = [this](auto&&... vn) {
        return this->state_->sf_(just((decltype(vn)&&)vn...));
      };
      [[maybe_unused]] auto none = [] {
        return just();
      };
      return (ItemSender &&) itemSender |  //
          let_value([this, none, item]([[maybe_unused]] auto&&... vn) {
               using alt_sender_t = typename alt_sender<
                   remove_cvref_t<decltype(item((decltype(vn)&&)vn...))>,
                   remove_cvref_t<decltype(none())>>::type;
               if (this->state_->keep_(std::as_const(vn)...)) {
                 return alt_sender_t(item((decltype(vn)&&)vn...));
               }
               return alt_sender_t(none());
             });
    }
    state_t* state_;
  };
  // state breaks the type recursion that causes op to be an incomplete type
  // when discard_ is accessed by filter_each
  using pred_op_t =
      sequence_connect_result_t<Predecessor, SuccessorReceiver, filter_each>;
  state_t state_;
  pred_op_t predOp_;

  template <
      typename Predecessor2,
      typename SuccessorReceiver2,
      typename SenderFactory2,
      typename Predicate2>
  type(
      Predecessor2&& predecessor,
      SuccessorReceiver2&& successorReceiver,
      SenderFactory2&& sf,
      Predicate2&& keep)
    : state_({(SenderFactory2 &&) sf, (Predicate2 &&) keep})
    , predOp_(sequence_connect(
          (Predecessor2 &&) predecessor,
          (SuccessorReceiver2 &&) successorReceiver,
          filter_each{&state_})) {}

  friend auto tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    return unifex::start(self.predOp_);
  }
};
template <typename Predecessor, typename Predicate>
struct filter_fn::sender<Predecessor, Predicate>::type {
  Predecessor predecessor_;
  Predicate keep_;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = sender_value_types_t<Predecessor, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = sender_error_types_t<Predecessor, Variant>;

  static constexpr bool sends_done = sender_traits<Predecessor>::sends_done;

  template <typename Sender, typename Receiver, typename SenderFactory>
  using op_t = typename op<Sender, Receiver, SenderFactory, Predicate>::type;

  template(typename T, typename Receiver, typename SenderFactory)  //
      (requires                                                    //
       (same_as<remove_cvref_t<T>, type>))                         //
      friend op_t<member_t<T, Predecessor>, Receiver, SenderFactory> tag_invoke(
          unifex::tag_t<unifex::sequence_connect>,
          T&& self,
          Receiver&& receiver,
          SenderFactory&& sf)  //
      noexcept(
          is_nothrow_callable_v<
              tag_t<unifex::sequence_connect>,
              member_t<T, Predecessor>,
              Receiver,
              typename op_t<member_t<T, Predecessor>, Receiver, SenderFactory>::
                  filter_each>) {
    return op_t<member_t<T, Predecessor>, Receiver, SenderFactory>(
        static_cast<T&&>(self).predecessor_,
        (Receiver &&) receiver,
        (SenderFactory &&) sf,
        static_cast<T&&>(self).keep_);
  }
};
}  // namespace _flt_cpo
using _flt_cpo::filter_each;
}  // namespace unifex
#include <unifex/detail/epilogue.hpp>
