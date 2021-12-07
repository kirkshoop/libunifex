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

#include <unifex/async_manual_reset_event.hpp>
#include <unifex/packaged_callable.hpp>
#include <unifex/resume_tail_sender.hpp>
#include <unifex/sequence_concepts.hpp>
#include <unifex/storage_concepts.hpp>
#include <unifex/then.hpp>
#include <unifex/variant_tail_sender.hpp>

#include <variant>

// fork() - a sequence algorithm
//
// takes a set of sequence senders and forks them into one
// sequence sender that it returns
//
// fixed overhead and fully-typed and supports lock-step
// can be made concurrent
//
//

#include <unifex/detail/prologue.hpp>
#include "unifex/stream_concepts.hpp"
namespace unifex {
namespace _frk_cpo {
inline constexpr struct fork_fn {
  template <typename Receiver, typename SenderFactory, typename Errors>
  struct _tail_start;
  template <typename Receiver, typename SenderFactory, typename Errors>
  struct _tail_restart;
  template <typename Receiver, typename SenderFactory, typename Errors>
  struct _tail_resume;
  template <typename Receiver, typename SenderFactory, typename Errors>
  struct storage_rcvr {
    struct type;
  };
  template <typename Receiver, typename SenderFactory, typename Errors>
  struct item_rcvr {
    struct type;
  };
  template <typename Receiver, typename SenderFactory, typename Errors>
  struct traits;
  template <typename Receiver, typename SenderFactory, typename Errors>
  struct _state {
    struct type;
  };
  template <typename Receiver, typename SenderFactory, typename Errors>
  using state = typename _state<Receiver, SenderFactory, Errors>::type;
  template <typename Receiver, typename SenderFactory, typename Errors>
  struct item_op {
    struct type;
  };
  template <typename Receiver, typename SenderFactory, typename Errors>
  struct storage_op {
    struct type;
  };
  template <typename Receiver, typename SenderFactory, typename Errors>
  struct op {
    struct type;
  };
  struct _sender {
    template <
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    // fork supports cancellation from the receiver token
    static constexpr bool sends_done = true;

    template <typename Receiver, typename SenderFactory>
    using op_t =
        typename op<Receiver, SenderFactory, error_types<std::variant>>::type;

    template(typename T, typename Receiver, typename SenderFactory)  //
        (requires                                                    //
         (same_as<remove_cvref_t<T>, _sender>))                      //
        friend op_t<Receiver, SenderFactory> tag_invoke(
            unifex::tag_t<unifex::sequence_connect>,
            T&&,
            Receiver&& receiver,
            SenderFactory&& sf)  //
        noexcept((is_nothrow_callable_v<
                  tag_t<unifex::connect>,
                  member_t<
                      T,
                      typename traits<
                          Receiver,
                          SenderFactory,
                          error_types<std::variant>>::storage_sender_t>,
                  typename storage_rcvr<
                      Receiver,
                      SenderFactory,
                      error_types<std::variant>>::type>)) {
      return op_t<Receiver, SenderFactory>(
          (Receiver &&) receiver, (SenderFactory &&) sf);
    }
  };
  inline constexpr auto operator()() const -> _sender { return {}; }
} fork;
template <typename Receiver, typename SenderFactory, typename Errors>
struct fork_fn::storage_rcvr<Receiver, SenderFactory, Errors>::type {
  using result_t = variant_tail_sender<
      null_tail_sender,
      callable_result_t<
          unifex::tag_t<unifex::tail>,
          _tail_restart<Receiver, SenderFactory, Errors>>,
      callable_result_t<
          unifex::tag_t<unifex::set_error>,
          Receiver,
          std::exception_ptr>,
      callable_result_t<unifex::tag_t<unifex::set_done>, Receiver>,
      callable_result_t<unifex::tag_t<unifex::set_value>, Receiver>>;

  template <typename RefOp>
  friend result_t
  tag_invoke(unifex::tag_t<unifex::set_value>, type&& self, RefOp&& refOp) {
    return {self.op_->storage(unifex::set_value, (RefOp &&) refOp)};
  }
  template <typename Error>
  friend result_t tag_invoke(
      unifex::tag_t<unifex::set_error>, type&& self, Error&& error) noexcept {
    return {self.op_->storage(unifex::set_error, (Error &&) error)};
  }
  friend result_t
  tag_invoke(unifex::tag_t<unifex::set_done>, type&& self) noexcept {
    return {self.op_->storage(unifex::set_done)};
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
  state<Receiver, SenderFactory, Errors>* op_;
};
template <typename Receiver, typename SenderFactory, typename Errors>
struct fork_fn::item_rcvr<Receiver, SenderFactory, Errors>::type {
  using traits_t = fork_fn::traits<Receiver, SenderFactory, Errors>;
  using aref_t = typename traits_t::aref_t;

  using result_t = variant_tail_sender<
      callable_result_t<
          unifex::tag_t<unifex::set_error>,
          Receiver,
          std::exception_ptr>,
      callable_result_t<unifex::tag_t<unifex::set_done>, Receiver>,
      callable_result_t<unifex::tag_t<unifex::set_value>, Receiver>>;

  friend result_t tag_invoke(unifex::tag_t<unifex::set_value>, type&& self) {
    auto op = self.op_;
    resume_tail_sender(
        as_tail_sender(unifex::destruct(self.op_->storage_, self.aref_op_)));
    return {op->complete(unifex::set_value)};
  }
  template <typename Error>
  friend result_t tag_invoke(
      unifex::tag_t<unifex::set_error>, type&& self, Error&& error) noexcept {
    auto op = self.op_;
    resume_tail_sender(
        as_tail_sender(unifex::destruct(self.op_->storage_, self.aref_op_)));
    return {op->complete(unifex::set_error, (Error &&) error)};
  }
  friend result_t
  tag_invoke(unifex::tag_t<unifex::set_done>, type&& self) noexcept {
    auto op = self.op_;
    resume_tail_sender(
        as_tail_sender(unifex::destruct(self.op_->storage_, self.aref_op_)));
    return {op->complete(unifex::set_done)};
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
  state<Receiver, SenderFactory, Errors>* op_;
  aref_t aref_op_;
};
template <typename Receiver, typename SenderFactory, typename Errors>
struct fork_fn::traits {
  using token_t = get_stop_token_result_t<Receiver>;
  using sched_t = get_scheduler_result_t<Receiver>;

  using storage_t = callable_result_t<tag_t<get_storage>, const Receiver&>;
  using aref_t = typename storage_t::any_ref_type;

  using sched_sender_t = schedule_result_t<sched_t>;
  using item_expression_t = callable_result_t<SenderFactory, sched_sender_t>;
  using item_rcvr_t =
      typename fork_fn::item_rcvr<Receiver, SenderFactory, Errors>::type;
  using item_op_inner_t = connect_result_t<item_expression_t, item_rcvr_t>;
  using item_op_t =
      typename fork_fn::item_op<Receiver, SenderFactory, Errors>::type;
  using item_op_manual_t = manual_lifetime<item_op_t>;

  using item_storage_t = callable_result_t<
      tag_t<get_storage_for<item_op_manual_t>>,
      const storage_t&>;
  using ref_t = typename item_storage_t::stg_ref;

  using storage_sender_t = callable_result_t<
      tag_t<unifex::construct>,
      item_storage_t&,
      item_op_manual_t>;
  using storage_rcvr_t =
      typename fork_fn::storage_rcvr<Receiver, SenderFactory, Errors>::type;
  using storage_op_inner_t = connect_result_t<storage_sender_t, storage_rcvr_t>;
  using construct_op_t =
      typename fork_fn::storage_op<Receiver, SenderFactory, Errors>::type;
  using construct_op_manual_t = manual_lifetime<construct_op_t>;

  using tail_sender_t = callable_result_t<
      tag_t<unifex::resume_tail_senders_until_one_remaining>,
      unifex::replace_void_with_null_tail_sender<
          unifex::next_tail_operation_t<construct_op_t>>,
      unifex::replace_void_with_null_tail_sender<
          unifex::next_tail_operation_t<item_op_t>>>;
};
template <typename Receiver, typename SenderFactory, typename Errors>
struct fork_fn::_state<Receiver, SenderFactory, Errors>::type {
  using traits_t = fork_fn::traits<Receiver, SenderFactory, Errors>;
  using sched_t = typename traits_t::sched_t;
  using token_t = typename traits_t::token_t;
  using item_op_manual_t = typename traits_t::item_op_manual_t;
  using storage_rcvr_t = typename traits_t::storage_rcvr_t;
  using item_storage_t = typename traits_t::item_storage_t;
  using construct_op_manual_t = typename traits_t::construct_op_manual_t;
  using ref_t = typename traits_t::ref_t;

  std::atomic_flag error_flag_;
  std::optional<Errors> error_;

  Receiver rec_;
  SenderFactory sf_;
  sched_t sched_;
  token_t token_;
  std::atomic<size_t> count_pending_;
  async_manual_reset_event done_;
  item_storage_t storage_;
  construct_op_manual_t construct_op_;

  template <typename Receiver2, typename SenderFactory2>
  explicit type(Receiver2&& rec, SenderFactory2&& sf)
    : error_flag_(false)
    , error_()
    , rec_((Receiver2 &&) rec)
    , sf_((SenderFactory2 &&) sf)
    , sched_(get_scheduler(rec_))
    , token_(get_stop_token(rec_))
    , count_pending_(0)
    , done_()
    , storage_(get_storage_for<item_op_manual_t>(get_storage(rec)))
    , construct_op_() {}

  using result_t = variant_tail_sender<
      null_tail_sender,
      callable_result_t<
          unifex::tag_t<unifex::tail>,
          _tail_restart<Receiver, SenderFactory, Errors>>,
      callable_result_t<
          unifex::tag_t<unifex::set_error>,
          Receiver,
          std::exception_ptr>,
      callable_result_t<unifex::tag_t<unifex::set_done>, Receiver>,
      callable_result_t<unifex::tag_t<unifex::set_value>, Receiver>>;

  template <typename CPO, typename... AN>
  result_t storage(CPO, AN... an) {
    if constexpr (
        same_as<unifex::tag_t<unifex::set_error>, CPO> ||
        same_as<unifex::tag_t<unifex::set_done>, CPO>) {
      return complete(CPO{}, (AN &&) an...);
    } else if constexpr (same_as<unifex::tag_t<unifex::set_value>, CPO>) {
      static_assert(
          sizeof...(AN) == 1,
          "fork: storage construct sender must complete with one argument");
      return tail(_tail_restart<Receiver, SenderFactory, Errors>{this, an...});
    } else {
      return null_tail_sender{};
    }
  }

  template <typename CPO, typename... AN>
  result_t complete(CPO, AN... an) {
    if constexpr (same_as<unifex::tag_t<unifex::set_error>, CPO>) {
      if (error_flag_.test_and_set()) {
        error_.emplace(an...);
      }
    }
    if (--count_pending_ == 0) {
      if (token_.stop_requested()) {
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
template <typename Receiver, typename SenderFactory, typename Errors>
struct fork_fn::item_op<Receiver, SenderFactory, Errors>::type {
  using st_t = fork_fn::state<Receiver, SenderFactory, Errors>;
  using traits_t = fork_fn::traits<Receiver, SenderFactory, Errors>;
  using item_op_inner_t = typename traits_t::item_op_inner_t;

  st_t* st_;
  item_op_inner_t op_;

  template <typename Sender, typename Receiver2>
  type(st_t* st, Sender&& s, Receiver2&& r)
    : st_(st)
    , op_(unifex::connect((Sender &&) s, (Receiver2 &&) r)) {}

  friend auto tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    return unifex::result_or_null_tail_sender(unifex::start, self.op_);
  }
};
template <typename Receiver, typename SenderFactory, typename Errors>
struct fork_fn::storage_op<Receiver, SenderFactory, Errors>::type {
  using st_t = fork_fn::state<Receiver, SenderFactory, Errors>;
  using traits_t = fork_fn::traits<Receiver, SenderFactory, Errors>;
  using storage_op_inner_t = typename traits_t::storage_op_inner_t;

  st_t* st_;
  storage_op_inner_t op_;

  template <typename Sender, typename Receiver2>
  type(st_t* st, Sender&& s, Receiver2&& r)
    : st_(st)
    , op_(unifex::connect((Sender &&) s, (Receiver2 &&) r)) {}

  friend auto tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    return unifex::result_or_null_tail_sender(unifex::start, self.op_);
  }
};
template <typename Receiver, typename SenderFactory, typename Errors>
struct fork_fn::op<Receiver, SenderFactory, Errors>::type {
  state<Receiver, SenderFactory, Errors> st_;

  template <typename Receiver2, typename SenderFactory2>
  type(Receiver2&& rec, SenderFactory2&& sf)
    : st_((Receiver2 &&) rec, (SenderFactory2 &&) sf) {}

  friend auto tag_invoke(unifex::tag_t<unifex::start>, type& self) noexcept {
    return tail(_tail_start<Receiver, SenderFactory, Errors>{&self.st_});
  }
};
template <typename Receiver, typename SenderFactory, typename Errors>
struct fork_fn::_tail_start {
  using st_t = fork_fn::state<Receiver, SenderFactory, Errors>;

  struct type : tail_operation_state_base {
    using traits_t = fork_fn::traits<Receiver, SenderFactory, Errors>;
    using item_op_manual_t = typename traits_t::item_op_manual_t;
    using storage_rcvr_t = typename traits_t::storage_rcvr_t;
    st_t* st_;
    auto start() noexcept {
      st_->construct_op_.construct(
          st_,
          unifex::construct(st_->storage_, item_op_manual_t{}),
          storage_rcvr_t{st_});
      ++st_->count_pending_;
      return unifex::result_or_null_tail_sender(
          unifex::start, st_->construct_op_.get());
    }
    void unwind() noexcept {
      ++st_->count_pending_;
      resume_tail_sender(st_->complete(unifex::set_done));
    }
  };
  st_t* st_;
  constexpr type operator()() noexcept { return {{}, st_}; }
};
template <typename Receiver, typename SenderFactory, typename Errors>
struct fork_fn::_tail_restart {
  using st_t = fork_fn::state<Receiver, SenderFactory, Errors>;
  using traits_t = fork_fn::traits<Receiver, SenderFactory, Errors>;
  using ref_t = typename traits_t::ref_t;

  struct type : tail_operation_state_base {
    using item_expression_t = typename traits_t::item_expression_t;
    using item_rcvr_t = typename traits_t::item_rcvr_t;
    using item_op_t = typename traits_t::item_op_t;
    using item_op_manual_t = typename traits_t::item_op_manual_t;
    using storage_rcvr_t = typename traits_t::storage_rcvr_t;

    static item_op_t
    make_item_op(st_t* st, item_expression_t&& s, item_rcvr_t&& r) noexcept {
      return {st, std::move(s), std::move(r)};
    }

    st_t* st_;
    ref_t ref_op_;

    using result_t = variant_tail_sender<
        null_tail_sender,
        callable_result_t<
            unifex::tag_t<unifex::tail>,
            _tail_resume<Receiver, SenderFactory, Errors>>>;

    result_t start() noexcept {
      if (st_->token_.stop_requested()) {
        unwind();
        return null_tail_sender{};
      }
      st_->construct_op_.destruct();
      st_->construct_op_.construct(
          st_,
          unifex::construct(st_->storage_, item_op_manual_t{}),
          storage_rcvr_t{st_});
      ref_op_.get().construct(packaged_callable(
          &make_item_op,
          st_,
          st_->sf_(unifex::schedule(st_->sched_)),
          item_rcvr_t{st_, ref_op_}));
      return tail(_tail_resume<Receiver, SenderFactory, Errors>{st_, ref_op_});
    }
    void unwind() noexcept {
      resume_tail_sender(st_->complete(unifex::set_done));
      st_->construct_op_.destruct();
    }
  };
  st_t* st_;
  ref_t ref_op_;
  constexpr type operator()() noexcept { return {{}, st_, ref_op_}; }
};
template <typename Receiver, typename SenderFactory, typename Errors>
struct fork_fn::_tail_resume {
  using st_t = fork_fn::state<Receiver, SenderFactory, Errors>;
  using traits_t = fork_fn::traits<Receiver, SenderFactory, Errors>;
  using ref_t = typename traits_t::ref_t;

  struct type : tail_operation_state_base {
    void start() noexcept {
      ++st_->count_pending_;
      return unifex::resume_tail_sender(
          unifex::resume_tail_senders_until_one_remaining(
              unifex::result_or_null_tail_sender(
                  unifex::start, st_->construct_op_.get()),
              unifex::result_or_null_tail_sender(
                  unifex::start, ref_op_.get().get())));
    }
    void unwind() noexcept {
      st_->construct_op_.destruct();
      resume_tail_sender(
          as_tail_sender(unifex::destruct(st_->storage_, ref_op_)));
      ++st_->count_pending_;
      resume_tail_sender(st_->complete(unifex::set_done));
    }
    st_t* st_;
    ref_t ref_op_;
  };
  st_t* st_;
  ref_t ref_op_;
  constexpr type operator()() noexcept { return {{}, st_, ref_op_}; }
};
}  // namespace _frk_cpo
using _frk_cpo::fork;
}  // namespace unifex
#include <unifex/detail/epilogue.hpp>
