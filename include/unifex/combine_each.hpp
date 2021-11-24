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

#include <unifex/packaged_callable.hpp>
#include <unifex/resume_tail_sender.hpp>
#include <unifex/sequence_concepts.hpp>
#include <unifex/then.hpp>
#include <unifex/variant_tail_sender.hpp>

#include <variant>

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
  template <typename Receiver, typename Errors>
  struct succ_rcvr {
    struct type;
  };
  template <typename Receiver, typename Errors>
  struct state;
  template <
      typename Receiver,
      typename SenderFactory,
      typename Errors,
      typename... SeqN>
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
template <typename Receiver, typename Errors>
struct combine_fn::succ_rcvr<Receiver, Errors>::type {
  using result_t = variant_tail_sender<
      callable_result_t<
          unifex::tag_t<unifex::set_error>,
          Receiver,
          std::exception_ptr>,
      callable_result_t<unifex::tag_t<unifex::set_done>, Receiver>,
      callable_result_t<unifex::tag_t<unifex::set_value>, Receiver>>;
  friend result_t tag_invoke(unifex::tag_t<unifex::set_value>, type&& self) {
    return {self.op_->complete(unifex::set_value)};
  }
  template <typename Error>
  friend result_t tag_invoke(
      unifex::tag_t<unifex::set_error>, type&& self, Error&& error) noexcept {
    return {self.op_->complete(unifex::set_error, (Error &&) error)};
  }
  friend result_t
  tag_invoke(unifex::tag_t<unifex::set_done>, type&& self) noexcept {
    return {self.op_->complete(unifex::set_done)};
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
  state<Receiver, Errors>* op_;
};
template <typename Receiver, typename Errors>
struct combine_fn::state {
  Receiver rec_;
  std::atomic<int> started_;
  std::atomic_flag error_flag_;
  std::optional<Errors> error_;

  template <typename Receiver2>
  explicit state(Receiver2&& rec)
    : rec_((Receiver2 &&) rec)
    , started_(0)
    , error_flag_(false)
    , error_() {}

  using result_t = variant_tail_sender<
      null_tail_sender,
      callable_result_t<
          unifex::tag_t<unifex::set_error>,
          Receiver,
          std::exception_ptr>,
      callable_result_t<unifex::tag_t<unifex::set_done>, Receiver>,
      callable_result_t<unifex::tag_t<unifex::set_value>, Receiver>>;
  template <typename CPO, typename... AN>
  result_t complete(CPO, AN... an) {
    if constexpr (same_as<unifex::tag_t<unifex::set_error>, CPO>) {
      if (error_flag_.test_and_set()) {
        error_.emplace(an...);
      }
    }
    if (--started_ == 0) {
      auto token = get_stop_token(rec_);
      if (token.stop_requested()) {
        // set_done calls are ignored. only a stop
        // request will result in set_done
        return result_or_null_tail_sender(set_done, std::move(rec_));
      } else if (error_flag_.test()) {
        // replay the first error encountered
        return {std::visit(
            [this](auto& e) {
              return result_or_null_tail_sender(set_error, std::move(rec_), e);
            },
            *error_)};
      } else {
        // this is the result for set_done and set_value cases
        return result_or_null_tail_sender(set_value, std::move(rec_));
      }
    } else {
      return null_tail_sender{};
    }
  }
};
template <
    typename Receiver,
    typename SenderFactory,
    typename Errors,
    typename... SeqN>
struct _tail_start {
  using op_t =
      typename combine_fn::op<Receiver, SenderFactory, Errors, SeqN...>::type;
  struct type : tail_operation_state_base {
    op_t* op_;
    auto start() noexcept {
      op_->st_.started_ = sizeof...(SeqN);
      return std::apply(
          [](auto&... op) {
            return unifex::resume_tail_senders_until_one_remaining(
                unifex::result_or_null_tail_sender(unifex::start, op)...);
          },
          op_->opn_);
    }
    void unwind() noexcept {
      resume_tail_sender(result_or_null_tail_sender(
          unifex::set_done(std::move(op_->receiver_))));
    }
  };
  op_t* op_;
  constexpr type operator()() noexcept { return {{}, op_}; }
};
template <
    typename Receiver,
    typename SenderFactory,
    typename Errors,
    typename... SeqN>
struct combine_fn::op<Receiver, SenderFactory, Errors, SeqN...>::type {
  using succ_rcvr_t = typename succ_rcvr<Receiver, Errors>::type;
  using op_t = std::tuple<
      sequence_connect_result_t<SeqN, succ_rcvr_t, SenderFactory>...>;
  state<Receiver, Errors> st_;
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
    : st_((Receiver2 &&) rec)
    , opn_(packaged_callable(
          sequence_connect,
          std::get<Idx>((SeqN2 &&) seqn),
          succ_rcvr_t{&st_},
          (SenderFactory2 &&) sf)...) {}

  friend auto tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    return tail(_tail_start<Receiver, SenderFactory, Errors, SeqN...>{&self});
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
  using op_t =
      typename op<Receiver, SenderFactory, error_types<std::variant>, SeqN...>::
          type;

  template(typename T, typename Receiver, typename SenderFactory)  //
      (requires                                                    //
       (same_as<remove_cvref_t<T>, type>))                         //
      friend op_t<Receiver, SenderFactory> tag_invoke(
          unifex::tag_t<unifex::sequence_connect>,
          T&& self,
          Receiver&& receiver,
          SenderFactory&& sf)  //
      noexcept(
          (is_nothrow_callable_v<
               tag_t<unifex::sequence_connect>,
               member_t<T, SeqN>,
               typename succ_rcvr<Receiver, error_types<std::variant>>::type,
               SenderFactory> &&
           ...)) {
    return op_t<Receiver, SenderFactory>(
        (Receiver &&) receiver,
        (SenderFactory &&) sf,
        static_cast<T&&>(self).seqn_,
        std::make_index_sequence<sizeof...(SeqN)>{});
  }
};
}  // namespace _cmb_cpo
using _cmb_cpo::combine_each;
}  // namespace unifex
#include <unifex/detail/epilogue.hpp>
