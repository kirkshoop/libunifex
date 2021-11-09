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

#include <unifex/async_trace.hpp>
#include <unifex/blocking.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/resume_tail_sender.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>

#include <atomic>
#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <variant>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _when_all {

template <
    std::size_t Index,
    template <std::size_t>
    class Receiver,
    typename... Senders>
struct _operation_tuple {
  struct type;
};
template <
    std::size_t Index,
    template <std::size_t>
    class Receiver,
    typename... Senders>
using operation_tuple =
    typename _operation_tuple<Index, Receiver, Senders...>::type;

template <
    typename Indicies,
    template <std::size_t>
    class Receiver,
    typename... Senders>
struct tail_start;

template <
    std::size_t... IdxN,
    template <std::size_t>
    class Receiver,
    typename... Senders>
struct tail_start<std::index_sequence<IdxN...>, Receiver, Senders...> {
  using op_t = operation_tuple<0, Receiver, Senders...>;
  struct type : tail_operation_state_base {
    auto start() noexcept {
      return resume_tail_senders_until_one_remaining(
          op_->template start<IdxN>()...);
    }

    void unwind() noexcept { stopSource_->request_stop(); }

    op_t* op_;
    inplace_stop_source* stopSource_;
  };
  op_t* op_;
  inplace_stop_source* stopSource_;
  constexpr type operator()() noexcept { return {{}, op_, stopSource_}; }
};

template <
    std::size_t Index,
    template <std::size_t>
    class Receiver,
    typename First,
    typename... Rest>
struct _operation_tuple<Index, Receiver, First, Rest...> {
  struct type;
};
template <
    std::size_t Index,
    template <std::size_t>
    class Receiver,
    typename First,
    typename... Rest>
struct _operation_tuple<Index, Receiver, First, Rest...>::type
  : operation_tuple<Index + 1, Receiver, Rest...> {
  template <typename Parent>
  explicit type(Parent& parent, First&& first, Rest&&... rest)
    : operation_tuple<Index + 1, Receiver, Rest...>{parent, (Rest &&) rest...}
    , op_(connect((First &&) first, Receiver<Index>{parent})) {}

  template(size_t Index2)  //
      (requires            //
       (Index2 > Index))   //
      auto start() {
    return operation_tuple<Index + 1, Receiver, Rest...>::template start<
        Index2>();
  }

  template(size_t Index2)  //
      (requires            //
       (Index2 == Index))  //
      auto start() {
    return result_or_null_tail_sender(unifex::start, op_);
  }

private:
  connect_result_t<First, Receiver<Index>> op_;
};

template <std::size_t Index, template <std::size_t> class Receiver>
struct _operation_tuple<Index, Receiver> {
  struct type;
};
template <std::size_t Index, template <std::size_t> class Receiver>
struct _operation_tuple<Index, Receiver>::type {
  template <typename Parent>
  explicit type(Parent&) noexcept {}

  template <std::size_t>
  null_tail_sender start() noexcept {
    return {};
  }
};

struct cancel_operation {
  inplace_stop_source& stopSource_;

  void operator()() noexcept { stopSource_.request_stop(); }
};

template <typename Receiver, typename... Senders>
struct _op {
  struct type;
};
template <typename Receiver, typename... Senders>
using operation = typename _op<remove_cvref_t<Receiver>, Senders...>::type;

template <typename... Errors>
using unique_decayed_error_types =
    concat_type_lists_unique_t<type_list<std::decay_t<Errors>>...>;

template <template <typename...> class Variant, typename... Senders>
using error_types = typename concat_type_lists_unique_t<
    sender_error_types_t<Senders, unique_decayed_error_types>...,
    type_list<std::exception_ptr>>::template apply<Variant>;

template <typename... Values>
using decayed_value_tuple = type_list<std::tuple<std::decay_t<Values>...>>;

template <typename Sender>
using value_variant_for_sender = typename sender_value_types_t<
    Sender,
    concat_type_lists_unique_t,
    decayed_value_tuple>::template apply<std::variant>;

template <typename Sender, typename Receiver>
struct _tail_sender_builder {
  template <typename... ArgN>
  using callable_result_set_value =
      type_list<callable_result_t<tag_t<unifex::set_value>, Receiver, ArgN...>>;

  using tail_sender_variant_values = typename sender_value_types_t<
      Sender,
      concat_type_lists_unique_t,
      callable_result_set_value>::template apply<variant_tail_sender>;

  template <typename... ErrN>
  using callable_result_set_error = variant_tail_sender<
      callable_result_t<tag_t<unifex::set_error>, Receiver, ErrN>...>;

  using tail_sender_variant_errors =
      sender_error_types_t<Sender, callable_result_set_error>;

  using tail_sender_t = variant_tail_sender<
      null_tail_sender,
      tail_sender_variant_values,
      tail_sender_variant_errors,
      callable_result_t<tag_t<unifex::set_done>, Receiver>>;
};
template <typename Sender, typename Receiver>
using tail_sender_builder =
    _tail_sender_builder<remove_cvref_t<Sender>, remove_cvref_t<Receiver>>;

template <size_t Index, typename Receiver, typename... Senders>
struct _element_receiver {
  struct type;
};
template <size_t Index, typename Receiver, typename... Senders>
using element_receiver =
    typename _element_receiver<Index, Receiver, Senders...>::type;

template <size_t Index, typename Receiver, typename... Senders>
struct _element_receiver<Index, Receiver, Senders...>::type final {
  using element_receiver = type;

  using tail_sender_t = variant_tail_sender<
      typename tail_sender_builder<Senders, Receiver>::tail_sender_t...>;

  operation<Receiver, Senders...>& op_;

  template <typename... Values>
  tail_sender_t set_value(Values&&... values) noexcept {
    UNIFEX_TRY {
      std::get<Index>(op_.values_)
          .emplace(
              std::in_place_type<std::tuple<std::decay_t<Values>...>>,
              (Values &&) values...);
      return op_.element_complete();
    }
    UNIFEX_CATCH(...) { return this->set_error(std::current_exception()); }
  }

  template <typename Error>
  tail_sender_t set_error(Error&& error) noexcept {
    if (!op_.doneOrError_.exchange(true, std::memory_order_relaxed)) {
      op_.error_.emplace(
          std::in_place_type<std::decay_t<Error>>, (Error &&) error);
      op_.stopSource_.request_stop();
    }
    return op_.element_complete();
  }

  tail_sender_t set_done() noexcept {
    if (!op_.doneOrError_.exchange(true, std::memory_order_relaxed)) {
      op_.stopSource_.request_stop();
    }
    return op_.element_complete();
  }

  Receiver& get_receiver() const { return op_.receiver_; }

  template(typename CPO, typename R)(
      requires is_receiver_query_cpo_v<CPO> AND same_as<R, element_receiver> AND is_callable_v<
          CPO,
          const Receiver&>) friend auto tag_invoke(CPO cpo, const R& r) noexcept(is_nothrow_callable_v<CPO, const Receiver&>)
      -> callable_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.get_receiver()));
  }

  inplace_stop_source& get_stop_source() const { return op_.stopSource_; }

  friend inplace_stop_token
  tag_invoke(tag_t<get_stop_token>, const element_receiver& r) noexcept {
    return r.get_stop_source().get_token();
  }

  template <typename Func>
  friend void tag_invoke(
      tag_t<visit_continuations>, const element_receiver& r, Func&& func) {
    std::invoke(func, r.get_receiver());
  }
};

template <typename Receiver, typename... Senders>
struct _op<Receiver, Senders...>::type {
  using operation = type;
  using receiver_type = Receiver;
  template <std::size_t Index, typename Receiver2, typename... Senders2>
  friend struct _element_receiver;
  template <std::size_t Index>
  using op_element_receiver = element_receiver<Index, Receiver, Senders...>;

  using tail_sender_t = variant_tail_sender<
      typename tail_sender_builder<Senders, Receiver>::tail_sender_t...>;

  explicit type(Receiver&& receiver, Senders&&... senders)
    : receiver_((Receiver &&) receiver)
    , ops_(*this, (Senders &&) senders...) {}

  auto start() noexcept {
    stopCallback_.construct(
        get_stop_token(receiver_), cancel_operation{stopSource_});

    return tail(tail_start<
                std::index_sequence_for<Senders...>,
                op_element_receiver,
                Senders...>{&ops_, &stopSource_});
  }

private:
  tail_sender_t element_complete() noexcept {
    if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      return deliver_result();
    }
    return null_tail_sender{};
  }

  tail_sender_t deliver_result() noexcept {
    stopCallback_.destruct();

    if (get_stop_token(receiver_).stop_requested()) {
      return unifex::set_done(std::move(receiver_));
    } else if (doneOrError_.load(std::memory_order_relaxed)) {
      if (error_.has_value()) {
        return std::visit(
            [this](auto&& error) {
              return unifex::set_error(
                  std::move(receiver_), (decltype(error))error);
            },
            std::move(error_.value()));
      } else {
        return unifex::set_done(std::move(receiver_));
      }
    } else {
      return deliver_value(std::index_sequence_for<Senders...>{});
    }
  }

  template <std::size_t... Indices>
  tail_sender_t deliver_value(std::index_sequence<Indices...>) noexcept {
    UNIFEX_TRY {
      return unifex::set_value(
          std::move(receiver_),
          std::get<Indices>(std::move(values_)).value()...);
    }
    UNIFEX_CATCH(...) {
      return unifex::set_error(std::move(receiver_), std::current_exception());
    }
  }

  std::tuple<
      std::optional<value_variant_for_sender<remove_cvref_t<Senders>>>...>
      values_;
  std::optional<error_types<std::variant, remove_cvref_t<Senders>...>> error_;
  std::atomic<std::size_t> refCount_{sizeof...(Senders)};
  std::atomic<bool> doneOrError_{false};
  inplace_stop_source stopSource_;
  UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<typename stop_token_type_t<
      Receiver&>::template callback_type<cancel_operation>>
      stopCallback_;
  Receiver receiver_;
  operation_tuple<0, op_element_receiver, Senders...> ops_;
};

template <typename... Senders>
struct _sender {
  class type;
};
template <typename... Senders>
using sender = typename _sender<remove_cvref_t<Senders>...>::type;

template <typename Receiver, typename Indices, typename... Senders>
extern const bool _when_all_connectable_v;

template <typename Receiver, std::size_t... Indices, typename... Senders>
inline constexpr bool _when_all_connectable_v<
    Receiver,
    std::index_sequence<Indices...>,
    Senders...> =
    (sender_to<Senders, element_receiver<Indices, Receiver, Senders...>> &&
     ...);

template <typename Receiver, typename... Senders>
inline constexpr bool when_all_connectable_v = _when_all_connectable_v<
    Receiver,
    std::index_sequence_for<Senders...>,
    Senders...>;

template <typename... Senders>
class _sender<Senders...>::type {
  using sender = type;

public:
  static_assert(sizeof...(Senders) > 0);

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<value_variant_for_sender<Senders>...>>;

  template <template <typename...> class Variant>
  using error_types = error_types<Variant, Senders...>;

  static constexpr bool sends_done = true;

  template <typename... Senders2>
  explicit type(Senders2&&... senders) : senders_((Senders2 &&) senders...) {}

  template(typename CPO, typename Sender, typename Receiver)(
      requires same_as<CPO, tag_t<unifex::connect>> AND
          same_as<remove_cvref_t<Sender>, type> AND when_all_connectable_v<
              remove_cvref_t<Receiver>,
              member_t<
                  Sender,
                  Senders>...>) friend auto tag_invoke([[maybe_unused]] CPO cpo, Sender&& sender, Receiver&& receiver)
      -> operation<Receiver, member_t<Sender, Senders>...> {
    return std::apply(
        [&](Senders&&... senders) {
          return operation<Receiver, member_t<Sender, Senders>...>{
              (Receiver &&) receiver, (Senders &&) senders...};
        },
        static_cast<Sender&&>(sender).senders_);
  }

private:
  // Customise the 'blocking' CPO to combine the blocking-nature
  // of each of the child operations.
  friend blocking_kind tag_invoke(tag_t<blocking>, const sender& s) noexcept {
    bool alwaysInline = true;
    bool alwaysBlocking = true;
    bool neverBlocking = false;

    auto handleBlockingStatus = [&](blocking_kind kind) noexcept {
      switch (kind) {
        case blocking_kind::never: neverBlocking = true; [[fallthrough]];
        case blocking_kind::maybe: alwaysBlocking = false; [[fallthrough]];
        case blocking_kind::always: alwaysInline = false; [[fallthrough]];
        case blocking_kind::always_inline: break;
      }
    };

    std::apply(
        [&](const auto&... senders) {
          (void)std::initializer_list<int>{
              (handleBlockingStatus(blocking(senders)), 0)...};
        },
        s.senders_);

    if (neverBlocking) {
      return blocking_kind::never;
    } else if (alwaysInline) {
      return blocking_kind::always_inline;
    } else if (alwaysBlocking) {
      return blocking_kind::always;
    } else {
      return blocking_kind::maybe;
    }
  }

  std::tuple<Senders...> senders_;
};

namespace _cpo {
struct _fn {
  template(typename... Senders)(requires(unifex::sender<Senders>&&...)
                                    AND tag_invocable<_fn, Senders...>) auto
  operator()(Senders&&... senders) const
      -> tag_invoke_result_t<_fn, Senders...> {
    return tag_invoke(*this, (Senders &&) senders...);
  }
  template(typename... Senders)(requires(typed_sender<Senders>&&...)
                                    AND(!tag_invocable<_fn, Senders...>)) auto
  operator()(Senders&&... senders) const -> _when_all::sender<Senders...> {
    return _when_all::sender<Senders...>{(Senders &&) senders...};
  }
};
}  // namespace _cpo
}  // namespace _when_all

inline constexpr _when_all::_cpo::_fn when_all{};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
