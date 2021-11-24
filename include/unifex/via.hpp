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
#include <unifex/get_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/submit.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/variant_tail_sender.hpp>

#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _via {

template <typename Receiver, typename... Values>
struct _value_receiver {
  struct type;
};
template <typename Receiver, typename... Values>
using value_receiver =
    typename _value_receiver<Receiver, std::decay_t<Values>...>::type;

template <typename Receiver, typename... Values>
struct _value_receiver<Receiver, Values...>::type {
  using value_receiver = type;
  UNIFEX_NO_UNIQUE_ADDRESS std::tuple<Values...> values_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  auto set_value() noexcept {
    return std::apply(
        [&](Values&&... values) noexcept {
          return unifex::set_value(
              std::forward<Receiver>(receiver_), (Values &&) values...);
        },
        std::move(values_));
  }

  template <typename Error>
  auto set_error(Error&& error) noexcept {
    return unifex::set_error(
        std::forward<Receiver>(receiver_), (Error &&) error);
  }

  auto set_done() noexcept {
    return unifex::set_done(std::forward<Receiver>(receiver_));
  }

  template(typename CPO)                                        //
      (requires                                                 //
       (is_receiver_query_cpo_v<CPO>))                          //
      friend auto tag_invoke(CPO cpo, const value_receiver& r)  //
      noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

  template <typename Func>
  friend void
  tag_invoke(tag_t<visit_continuations>, const value_receiver& r, Func&& func) {
    std::invoke(func, r.receiver_);
  }
};

template <typename Receiver, typename Error>
struct _error_receiver {
  struct type;
};
template <typename Receiver, typename Error>
using error_receiver =
    typename _error_receiver<Receiver, std::decay_t<Error>>::type;

template <typename Receiver, typename Error>
struct _error_receiver<Receiver, Error>::type {
  using error_receiver = type;
  UNIFEX_NO_UNIQUE_ADDRESS Error error_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  auto set_value() noexcept {
    return unifex::set_error(
        std::forward<Receiver>(receiver_), std::move(error_));
  }

  template <typename OtherError>
  auto set_error(OtherError&& otherError) noexcept {
    return unifex::set_error(
        std::forward<Receiver>(receiver_), (OtherError &&) otherError);
  }

  auto set_done() noexcept {
    return unifex::set_done(std::forward<Receiver>(receiver_));
  }

  template(typename CPO)                                        //
      (requires                                                 //
       (is_receiver_query_cpo_v<CPO>))                          //
      friend auto tag_invoke(CPO cpo, const error_receiver& r)  //
      noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

  template <typename Func>
  friend void
  tag_invoke(tag_t<visit_continuations>, const error_receiver& r, Func&& func) {
    std::invoke(func, r.receiver_);
  }
};

template <typename Receiver>
struct _done_receiver {
  struct type;
};
template <typename Receiver>
using done_receiver = typename _done_receiver<Receiver>::type;

template <typename Receiver>
struct _done_receiver<Receiver>::type {
  using done_receiver = type;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  auto set_value() noexcept {
    return unifex::set_done(std::forward<Receiver>(receiver_));
  }

  template <typename OtherError>
  auto set_error(OtherError&& otherError) noexcept {
    return unifex::set_error(
        std::forward<Receiver>(receiver_), (OtherError &&) otherError);
  }

  auto set_done() noexcept {
    return unifex::set_done(std::forward<Receiver>(receiver_));
  }

  template(typename CPO)                                       //
      (requires                                                //
       (is_receiver_query_cpo_v<CPO>))                         //
      friend auto tag_invoke(CPO cpo, const done_receiver& r)  //
      noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

  template <typename Func>
  friend void
  tag_invoke(tag_t<visit_continuations>, const done_receiver& r, Func&& func) {
    std::invoke(func, r.receiver_);
  }
};

template <typename Successor, typename Receiver>
struct _predecessor_receiver {
  struct type;
};
template <typename Successor, typename Receiver>
using predecessor_receiver =
    typename _predecessor_receiver<Successor, remove_cvref_t<Receiver>>::type;

template <typename Successor, typename Receiver>
struct _predecessor_receiver<Successor, Receiver>::type {
  using predecessor_receiver = type;
  Successor successor_;
  Receiver receiver_;

  using eptr_tail_t =
      callable_result_t<tag_t<set_error>, Receiver, std::exception_ptr>;

  template <typename... Values>
  using value_rec_t = value_receiver<Receiver, Values...>;
  template <typename... Values>
  using value_tail_t = variant_tail_sender<
      eptr_tail_t,
      callable_result_t<
          tag_t<unifex::submit>,
          Successor,
          value_rec_t<Values...>>>;

  template <typename... Values>
  value_tail_t<Values...> set_value(Values&&... values) && noexcept {
    UNIFEX_TRY {
      return result_or_null_tail_sender(
          unifex::submit,
          (Successor &&) successor_,
          value_rec_t<Values...>{
              {(Values &&) values...}, (Receiver &&) receiver_});
    }
    UNIFEX_CATCH(...) {
      return result_or_null_tail_sender(
          unifex::set_error,
          static_cast<Receiver&&>(receiver_),
          std::current_exception());
    }
  }

  template <typename Error>
  using error_rec_t = error_receiver<Receiver, Error>;
  template <typename Error>
  using error_tail_t = variant_tail_sender<
      eptr_tail_t,
      callable_result_t<tag_t<unifex::submit>, Successor, error_rec_t<Error>>>;

  template <typename Error>
  error_tail_t<Error> set_error(Error&& error) && noexcept {
    UNIFEX_TRY {
      return result_or_null_tail_sender(
          unifex::submit,
          (Successor &&) successor_,
          error_rec_t<Error>{(Error &&) error, (Receiver &&) receiver_});
    }
    UNIFEX_CATCH(...) {
      return result_or_null_tail_sender(
          unifex::set_error,
          static_cast<Receiver&&>(receiver_),
          std::current_exception());
    }
  }

  using done_tail_t = variant_tail_sender<
      eptr_tail_t,
      callable_result_t<
          tag_t<unifex::submit>,
          Successor,
          done_receiver<Receiver>>>;

  done_tail_t set_done() && noexcept {
    UNIFEX_TRY {
      return result_or_null_tail_sender(
          unifex::submit,
          (Successor &&) successor_,
          done_receiver<Receiver>{(Receiver &&) receiver_});
    }
    UNIFEX_CATCH(...) {
      return result_or_null_tail_sender(
          unifex::set_error,
          static_cast<Receiver&&>(receiver_),
          std::current_exception());
    }
  }

  template(typename CPO)                                              //
      (requires                                                       //
       (is_receiver_query_cpo_v<CPO>))                                //
      friend auto tag_invoke(CPO cpo, const predecessor_receiver& r)  //
      noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>, const predecessor_receiver& r, Func&& func) {
    std::invoke(func, r.receiver_);
  }
};

template <typename Predecessor, typename Successor>
struct _sender {
  struct type;
};
template <typename Predecessor, typename Successor>
using sender =
    typename _sender<remove_cvref_t<Predecessor>, remove_cvref_t<Successor>>::
        type;

template <typename Predecessor, typename Successor>
struct _sender<Predecessor, Successor>::type {
  using sender = type;
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Successor succ_;

  template <typename... Ts>
  using overload_list = type_list<type_list<std::decay_t<Ts>...>>;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = type_list_nested_apply_t<
      sender_value_types_t<
          Predecessor,
          concat_type_lists_unique_t,
          overload_list>,
      Variant,
      Tuple>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Predecessor, type_list>,
      sender_error_types_t<Successor, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = sender_traits<Predecessor>::sends_done ||
      sender_traits<Successor>::sends_done;

  friend constexpr blocking_kind
  tag_invoke(tag_t<blocking>, const sender& sender) {
    const auto predBlocking = blocking(sender.pred_);
    const auto succBlocking = blocking(sender.succ_);
    if (predBlocking == blocking_kind::never &&
        succBlocking == blocking_kind::never) {
      return blocking_kind::never;
    } else if (
        predBlocking == blocking_kind::always_inline &&
        succBlocking == blocking_kind::always_inline) {
      return blocking_kind::always_inline;
    } else if (
        (predBlocking == blocking_kind::always_inline ||
         predBlocking == blocking_kind::always) &&
        (succBlocking == blocking_kind::always_inline ||
         succBlocking == blocking_kind::always)) {
      return blocking_kind::always;
    } else {
      return blocking_kind::maybe;
    }
  }

  template(typename Self, typename Receiver)      //
      (requires                                   //
       (same_as<remove_cvref_t<Self>, type>) AND  //
       (sender_to<
           member_t<Self, Predecessor>,
           predecessor_receiver<Successor, Receiver>>))  //
      friend auto tag_invoke(
          tag_t<unifex::connect>,
          Self&& s,
          Receiver&& receiver)  //
      noexcept((is_nothrow_connectable_v<
                member_t<Self, Predecessor>,
                predecessor_receiver<Successor, Receiver>>))
          -> connect_result_t<
              member_t<Self, Predecessor>,
              predecessor_receiver<Successor, Receiver>> {
    return unifex::connect(
        static_cast<Self&&>(s).pred_,
        predecessor_receiver<Successor, Receiver>{
            static_cast<Self&&>(s).succ_, static_cast<Receiver&&>(receiver)});
  }

  template(typename Self, typename Receiver, typename SenderFactory)  //
      (requires                                                       //
       (same_as<remove_cvref_t<Self>, type>) AND                      //
       (sequence_sender_to<
           member_t<Self, Predecessor>,
           predecessor_receiver<Successor, Receiver>,
           SenderFactory>))  //
      friend auto tag_invoke(
          tag_t<unifex::sequence_connect>,
          Self&& s,
          Receiver&& receiver,
          SenderFactory&& sf)  //
      noexcept((is_nothrow_callable_v<
                tag_t<unifex::sequence_connect>,
                member_t<Self, Predecessor>,
                predecessor_receiver<Successor, Receiver>,
                SenderFactory>))
          -> sequence_connect_result_t<
              member_t<Self, Predecessor>,
              predecessor_receiver<Successor, Receiver>,
              SenderFactory> {
    return unifex::sequence_connect(
        static_cast<Self&&>(s).pred_,
        predecessor_receiver<Successor, Receiver>{
            static_cast<Self&&>(s).succ_, static_cast<Receiver&&>(receiver)},
        static_cast<SenderFactory&&>(sf));
  }
};
}  // namespace _via

namespace _via_cpo {
inline const struct _fn {
  template(typename Scheduler, typename Sender)  //
      (requires                                  //
       (scheduler<Scheduler>) AND                //
       (sender<Sender>))                         //
      auto
      operator()(Scheduler&& sched, Sender&& send) const  //
      noexcept(noexcept(_via::sender<Sender, schedule_result_t<Scheduler>>{
          (Sender &&) send, schedule(sched)}))
          -> _via::sender<Sender, schedule_result_t<Scheduler>> {
    return _via::sender<Sender, schedule_result_t<Scheduler>>{
        (Sender &&) send, schedule(sched)};
  }
} via{};
}  // namespace _via_cpo

using _via_cpo::via;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
