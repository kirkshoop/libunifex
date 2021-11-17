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
#include <unifex/async_trace.hpp>
#include <unifex/bind_back.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/resume_tail_sender.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/tail_sender_concepts.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/variant_tail_sender.hpp>

#include <exception>
#include <utility>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _repeat_effect_until {
template <typename Source, typename Predicate, typename Receiver>
struct _op {
  class type;
};
template <typename Source, typename Predicate, typename Receiver>
using operation_type = typename _op<Source, Predicate, Receiver>::type;

template <typename Source, typename Predicate, typename Receiver>
struct _rcvr {
  class type;
};
template <typename Source, typename Predicate, typename Receiver>
using receiver_t = typename _rcvr<Source, Predicate, Receiver>::type;

template <typename Source, typename Predicate>
struct _sndr {
  class type;
};

template <typename Source, typename Predicate, typename Receiver>
struct _tail_unwind {
  using operation = operation_type<Source, Predicate, Receiver>;
  struct type : tail_operation_state_base {
    operation* op_;
    void unwind() noexcept {
      using tail = callable_result_t<tag_t<unifex::set_done>, Receiver>;
      if constexpr (tail_sender<tail>) {
        resume_tail_sender(unifex::set_done(std::move(op_->receiver_)));
      } else if constexpr (sender<tail>) {
        static_assert(
            !sender<tail>, "repeat_effect_until: sender not yet supported");
      } else if constexpr (std::is_void_v<tail>) {
        unifex::set_done(std::move(op_->receiver_));
      } else {
        static_assert(
            !std::is_void_v<tail>,
            "repeat_effect_until: unsupported set_done result");
      }
    }
  };
};

template <typename Source, typename Predicate, typename Receiver>
struct _tail_start {
  using operation = operation_type<Source, Predicate, Receiver>;
  struct type : _tail_unwind<Source, Predicate, Receiver>::type {
    template(typename... Args)            //
        (requires(sizeof...(Args) == 0))  //
        auto start(Args...) noexcept {
      return unifex::start(this->op_->sourceOp_.get());
    }
  };
  operation* op_;
  constexpr type operator()() noexcept { return {{{}, op_}}; }
};

template <typename Source, typename Predicate, typename Receiver>
struct _tail_restart {
  using operation = operation_type<Source, Predicate, Receiver>;
  struct type : _tail_unwind<Source, Predicate, Receiver>::type {
    template <typename Tail>
    auto repeat()                                        //
        noexcept(                                        //
            (std::is_nothrow_invocable_v<Predicate&>)&&  //
            (is_nothrow_connectable_v<Source&, type>)&&  //
            (is_nothrow_tag_invocable_v<tag_t<unifex::set_value>, Receiver>)) {
      auto* op = this->op_;
      // call predicate and complete with void if it returns true
      if (op->predicate_()) {
        return Tail{result_or_null_tail_sender(
            unifex::set_value, std::move(op->receiver_))};
      }
      op->sourceOp_.construct_with([&] {
        return unifex::connect(
            op->source_, typename _rcvr<Source, Predicate, Receiver>::type{op});
      });
      op->isSourceOpConstructed_ = true;
      return Tail{tail(_tail_start<Source, Predicate, Receiver>{op})};
    }
    template(typename... Args)            //
        (requires(sizeof...(Args) == 0))  //
        auto start(Args...) noexcept {
      using tail_t = variant_tail_sender<
          null_tail_sender,
          decltype(tail(
              std::declval<_tail_start<Source, Predicate, Receiver>&>())),
          callable_result_t<tag_t<unifex::set_value>, Receiver>,
          callable_result_t<
              tag_t<unifex::set_error>,
              Receiver,
              std::exception_ptr>>;
      auto* op = this->op_;

      if (op->stop_.stop_requested()) {
        this->unwind();
        return tail_t{null_tail_sender{}};
      }

      UNIFEX_ASSERT(op->isSourceOpConstructed_);
      op->isSourceOpConstructed_ = false;
      op->sourceOp_.destruct();

      if constexpr (noexcept(repeat<tail_t>())) {
        return repeat<tail_t>();
      } else {
        UNIFEX_TRY { return repeat<tail_t>(); }
        UNIFEX_CATCH(...) {
          return tail_t{result_or_null_tail_sender(
              unifex::set_error,
              std::move(op->receiver_),
              std::current_exception())};
        }
      }
    }
  };
  operation* op_;
  constexpr type operator()() noexcept { return {{{}, op_}}; }
};

template <typename Source, typename Predicate, typename Receiver>
class _rcvr<Source, Predicate, Receiver>::type {
  using operation = operation_type<Source, Predicate, Receiver>;
  using tail_done = callable_result_t<tag_t<unifex::set_done>, Receiver>;
  template <typename Error>
  using tail_error =
      callable_result_t<tag_t<unifex::set_error>, Receiver, Error>;

public:
  explicit type(operation* op) noexcept : op_(op) {}

  type(type&& other) noexcept : op_(std::exchange(other.op_, {})) {}

  // This signals to repeat the operation.
  template(typename... Args)            //
      (requires(sizeof...(Args) == 0))  //
      decltype(tail(
          std::declval<_tail_restart<Source, Predicate, Receiver>&>()))
          set_value(Args...) noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    return tail(_tail_restart<Source, Predicate, Receiver>{op_});
  }

  template(typename... Args)            //
      (requires(sizeof...(Args) == 0))  //
      tail_done set_done(Args...) noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    return unifex::set_done(std::move(op_->receiver_));
  }

  template(typename Error)                  //
      (requires receiver<Receiver, Error>)  //
      tail_error<Error> set_error(Error&& error) noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    return unifex::set_error(std::move(op_->receiver_), (Error &&) error);
  }

private:
  template(typename CPO)                   //
      (requires                            //
       (is_receiver_query_cpo_v<CPO>) AND  //
       (is_callable_v<
           CPO,
           const Receiver&>))                         //
      friend auto tag_invoke(CPO cpo, const type& r)  //
      noexcept(std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(r.get_rcvr());
  }

  template <typename VisitFunc>
  friend void tag_invoke(
      tag_t<visit_continuations>,
      const type& r,
      VisitFunc&& func)  //
      noexcept(std::is_nothrow_invocable_v<VisitFunc&, const Receiver&>) {
    std::invoke(func, r.get_rcvr());
  }

  const Receiver& get_rcvr() const noexcept {
    UNIFEX_ASSERT(op_ != nullptr);
    return op_->receiver_;
  }

  operation* op_;
};

template <typename Source, typename Predicate, typename Receiver>
class _op<Source, Predicate, Receiver>::type {
  using _stop_token_t = get_stop_token_result_t<Receiver>;

public:
  template <typename Source2, typename Predicate2, typename Receiver2>
  explicit type(
      Source2&& source,
      Predicate2&& predicate,
      Receiver2&& dest)                                                    //
      noexcept((std::is_nothrow_constructible_v<Receiver, Receiver2>)&&    //
               (std::is_nothrow_constructible_v<Predicate, Predicate2>)&&  //
               (std::is_nothrow_constructible_v<Source, Source2>)&&        //
               is_nothrow_connectable_v<
                   Source&,
                   receiver_t<Source, Predicate, Receiver>>)
    : source_((Source2 &&) source)
    , predicate_((Predicate2 &&) predicate)
    , receiver_((Receiver2 &&) dest)
    , stop_(unifex::get_stop_token(receiver_)) {
    sourceOp_.construct_with([&] {
      return unifex::connect(
          source_, receiver_t<Source, Predicate, Receiver>{this});
    });
  }

  ~type() {
    if (isSourceOpConstructed_) {
      sourceOp_.destruct();
      isSourceOpConstructed_ = false;
    }
  }

  template(typename... Args)(requires(sizeof...(Args) == 0)) auto start(
      Args...) & noexcept {
    return unifex::start(sourceOp_.get());
  }

private:
  friend receiver_t<Source, Predicate, Receiver>;
  friend struct _tail_unwind<Source, Predicate, Receiver>::type;
  friend struct _tail_start<Source, Predicate, Receiver>::type;
  friend struct _tail_restart<Source, Predicate, Receiver>::type;

  using source_op_t =
      connect_result_t<Source&, receiver_t<Source, Predicate, Receiver>>;

  UNIFEX_NO_UNIQUE_ADDRESS Source source_;
  UNIFEX_NO_UNIQUE_ADDRESS Predicate predicate_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
  _stop_token_t stop_;
  bool isSourceOpConstructed_ = true;
  manual_lifetime<source_op_t> sourceOp_;
};

template <typename Source, typename Predicate>
class _sndr<Source, Predicate>::type {
public:
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<>>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Source, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = true;

  template <typename Source2, typename Predicate2>
  explicit type(Source2&& source, Predicate2&& predicate) noexcept(
      std::is_nothrow_constructible_v<Source, Source2>&&
          std::is_nothrow_constructible_v<Predicate, Predicate2>)
    : source_((Source2 &&) source)
    , predicate_((Predicate2 &&) predicate) {}

  template(typename Sender, typename Receiver)  //
      (requires                                 //
       (receiver<Receiver>) AND                 //
       (same_as<
           remove_cvref_t<Sender>,
           type>) AND  //
       (constructible_from<
           remove_cvref_t<Receiver>,
           Receiver>) AND        //
       (receiver<Receiver>) AND  //
           sender_to<
               Source&,
               receiver_t<Source, Predicate, remove_cvref_t<Receiver>>>)  //
      friend auto tag_invoke(
          tag_t<unifex::connect>,
          Sender&& s,
          Receiver&& r)  //
      noexcept((std::is_nothrow_constructible_v<
                Source,
                decltype((static_cast<Sender&&>(s).source_))>)&&  //
               (std::is_nothrow_constructible_v<
                   Predicate,
                   decltype((static_cast<Sender&&>(s).predicate_))>)&&  //
               (std::is_nothrow_constructible_v<
                   remove_cvref_t<Receiver>,
                   Receiver>)&&  //
               is_nothrow_connectable_v<
                   Source&,
                   receiver_t<
                       Source,
                       Predicate,
                       remove_cvref_t<Receiver>>>)  //
      -> operation_type<Source, Predicate, remove_cvref_t<Receiver>> {
    return operation_type<Source, Predicate, remove_cvref_t<Receiver>>{
        static_cast<Sender&&>(s).source_,
        static_cast<Sender&&>(s).predicate_,
        (Receiver &&) r};
  }

private:
  UNIFEX_NO_UNIQUE_ADDRESS Source source_;
  UNIFEX_NO_UNIQUE_ADDRESS Predicate predicate_;
};

}  // namespace _repeat_effect_until

template <class Source, class Predicate>
using repeat_effect_until_sender =
    typename _repeat_effect_until::_sndr<Source, Predicate>::type;

inline const struct repeat_effect_until_cpo {
  template <typename Source, typename Predicate>
  auto operator()(Source&& source, Predicate&& predicate) const noexcept(
      is_nothrow_tag_invocable_v<repeat_effect_until_cpo, Source, Predicate>)
      -> tag_invoke_result_t<repeat_effect_until_cpo, Source, Predicate> {
    return tag_invoke(*this, (Source &&) source, (Predicate &&) predicate);
  }

  template(typename Source, typename Predicate)                          //
      (requires                                                          //
       (!tag_invocable<repeat_effect_until_cpo, Source, Predicate>) AND  //
       (constructible_from<remove_cvref_t<Source>, Source>) AND          //
           constructible_from<std::decay_t<Predicate>, Predicate>)       //
      auto
      operator()(Source&& source, Predicate&& predicate) const  //
      noexcept(std::is_nothrow_constructible_v<
               repeat_effect_until_sender<
                   remove_cvref_t<Source>,
                   std::decay_t<Predicate>>,
               Source,
               Predicate>)
          -> repeat_effect_until_sender<
              remove_cvref_t<Source>,
              std::decay_t<Predicate>> {
    return repeat_effect_until_sender<
        remove_cvref_t<Source>,
        std::decay_t<Predicate>>{(Source &&) source, (Predicate &&) predicate};
  }
  template <typename Predicate>
  constexpr auto operator()(Predicate&& predicate) const
      noexcept(is_nothrow_callable_v<
               tag_t<bind_back>,
               repeat_effect_until_cpo,
               Predicate>)
          -> bind_back_result_t<repeat_effect_until_cpo, Predicate> {
    return bind_back(*this, (Predicate &&) predicate);
  }
} repeat_effect_until{};

inline const struct repeat_effect_cpo {
  struct forever {
    bool operator()() const { return false; }
  };
  template <typename Source>
  auto operator()(Source&& source) const
      noexcept(is_nothrow_tag_invocable_v<repeat_effect_cpo, Source>)
          -> tag_invoke_result_t<repeat_effect_cpo, Source> {
    return tag_invoke(*this, (Source &&) source);
  }

  template(typename Source)                                     //
      (requires                                                 //
       (!tag_invocable<repeat_effect_cpo, Source>) AND          //
           constructible_from<remove_cvref_t<Source>, Source>)  //
      auto
      operator()(Source&& source) const  //
      noexcept(std::is_nothrow_constructible_v<
               repeat_effect_until_sender<remove_cvref_t<Source>, forever>,
               Source>)
          -> repeat_effect_until_sender<remove_cvref_t<Source>, forever> {
    return repeat_effect_until_sender<remove_cvref_t<Source>, forever>{
        (Source &&) source, forever{}};
  }
  constexpr auto operator()() const
      noexcept(is_nothrow_callable_v<tag_t<bind_back>, repeat_effect_cpo>)
          -> bind_back_result_t<repeat_effect_cpo> {
    return bind_back(*this);
  }
} repeat_effect{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
