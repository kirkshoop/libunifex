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

#include <unifex/resume_tail_sender.hpp>
#include <unifex/sequence_concepts.hpp>
#include <unifex/then.hpp>

#include <unifex/packaged_callable.hpp>

// combine_each() - a sequence algorithm
//
// takes a set of sequence senders and combines them into one
// sequence sender that it returns
//
// fixed overhead and fully-typed and supports lock-step
// can be made concurrent
//
//

#include <unifex/detail/prologue.hpp>
namespace unifex {
namespace _cmb_cpo {
inline constexpr struct combine_fn {
  template <typename Receiver>
  struct succ_rcvr {
    struct type;
  };
  template <typename Receiver>
  struct state;
  template <typename Receiver, typename SenderFactory, typename... SeqN>
  struct op {
    struct type;
  };
  template <typename... SeqN>
  struct sender {
    struct type;
  };
  template <typename... SeqN>
  auto operator()(SeqN&&... seqN) const -> typename sender<SeqN...>::type {
    return {std::make_tuple(std::move(seqN)...)};
  }
} combine_each;
template <typename Receiver>
struct combine_fn::succ_rcvr<Receiver>::type {
  friend auto tag_invoke(unifex::tag_t<unifex::set_value>, type&& self) {
    return unifex::set_value(std::move(self.op_->rec_));
  }
  template <typename Error>
  friend auto tag_invoke(
      unifex::tag_t<unifex::set_error>, type&& self, Error&& error) noexcept {
    auto op = self.op_;
    // end sequence with the error
    return unifex::set_error(std::move(op->rec_), (Error &&) error);
  }
  friend auto
  tag_invoke(unifex::tag_t<unifex::set_done>, type&& self) noexcept {
    auto op = self.op_;
    // end sequence with the cancellation
    return unifex::set_done(std::move(op->rec_));
  }
  template(typename CPO, typename R)               //
      (requires                                    //
       (unifex::is_receiver_query_cpo_v<CPO>) AND  //
       (unifex::same_as<R, type>) AND              //
       (unifex::is_callable_v<
           CPO,
           const Receiver&>))  //
      friend auto tag_invoke(CPO cpo, const R& r) noexcept(
          unifex::is_nothrow_callable_v<CPO, const Receiver&>)
          -> unifex::callable_result_t<CPO, const Receiver&> {
    return static_cast<CPO&&>(cpo)(std::as_const(r.op_->rec_));
  }
  template <typename Func>
  friend void tag_invoke(
      unifex::tag_t<unifex::visit_continuations>, const type& r, Func&& func) {
    std::invoke(func, std::as_const(r.get_receiver()));
  }
  state<Receiver>* op_;
};
template <typename Receiver>
struct combine_fn::state {
  Receiver rec_;
};
template <typename Receiver, typename SenderFactory, typename... SeqN>
struct combine_fn::op<Receiver, SenderFactory, SeqN...>::type {
  using succ_rcvr_t = typename succ_rcvr<Receiver>::type;
  using op_t = std::tuple<
      sequence_connect_result_t<SeqN, succ_rcvr_t, SenderFactory>...>;
  state<Receiver> st_;
  op_t opn_;

  template <
      typename Receiver2,
      typename SenderFactory2,
      typename SeqN2,
      size_t... Idx>
  type(
      Receiver2&& rec,
      SenderFactory2&& sf,
      SeqN2&& seqn,
      std::index_sequence<Idx...>)
    : st_({(Receiver2 &&) rec})
    , opn_(packaged_callable(
          sequence_connect,
          std::get<Idx>((SeqN2 &&) seqn),
          succ_rcvr_t{&st_},
          (SenderFactory2 &&) sf)...) {}

  friend auto tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    // need to put this into a tail_sender and return that tail_sender
    return std::apply(
        [](auto&... op) {
          return unifex::resume_tail_senders_until_one_remaining(
              unifex::result_or_null_tail_sender(unifex::start, op)...);
        },
        self.opn_);
  }
};
template <typename... SeqN>
struct combine_fn::sender<SeqN...>::type {
  std::tuple<remove_cvref_t<SeqN>...> seqn_;
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = typename concat_type_lists_unique_t<
      sender_value_types_t<SeqN, type_list, Tuple>...>::template apply<Variant>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<SeqN, type_list>...,
      type_list<std::exception_ptr>>::template apply<Variant>;

  // combine supports cancellation from the receiver token, and suppresses
  // cancellation from the SeqN senders.
  static constexpr bool sends_done = true;

  template <typename Receiver, typename SenderFactory>
  friend typename op<Receiver, SenderFactory, SeqN...>::type tag_invoke(
      unifex::tag_t<unifex::sequence_connect>,
      const type& self,
      Receiver&& receiver,
      SenderFactory&& sf) {
    return {
        (Receiver &&) receiver,
        (SenderFactory &&) sf,
        self.seqn_,
        std::make_index_sequence<sizeof...(SeqN)>{}};
  }
};
}  // namespace _cmb_cpo
using _cmb_cpo::combine_each;
}  // namespace unifex
#include <unifex/detail/epilogue.hpp>
