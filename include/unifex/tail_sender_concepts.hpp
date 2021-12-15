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

#include <unifex/detail/unifex_fwd.hpp>

#include <unifex/blocking.hpp>
#include <unifex/packaged_callable.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>

#include <algorithm>
#include <exception>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

////////////////////////////////////////////////
// Tail Callable Concepts

template <typename T>
using next_tail_operation_t =
    unifex::callable_result_t<unifex::tag_t<unifex::start>, T&>;

template <typename T>
static constexpr bool noexcept_unwind = noexcept(std::declval<T>().unwind());

template <typename T>
UNIFEX_CONCEPT_FRAGMENT(
    _tail_operation_impl,
    requires(T& c)                                              //
        (c.unwind(), unifex::start(c)) &&                       //
        noexcept_unwind<T> &&                                   //
        same_as<decltype(std::declval<T>().unwind()), void> &&  //
        unifex::is_nothrow_callable_v<unifex::tag_t<unifex::start>, T&>);

template <typename T>
UNIFEX_CONCEPT
_tail_operation =
    ((!std::is_copy_constructible_v<T>)&&                                   //
     (!std::is_move_constructible_v<T>)&&(!std::is_copy_assignable_v<T>)&&  //
     (!std::is_move_assignable_v<T>)&&                                      //
     (std::is_nothrow_destructible_v<T>)&&                                  //
     (std::is_trivially_destructible_v<T>)&&                                //
     UNIFEX_FRAGMENT(_tail_operation_impl, T));

template <typename T, typename Receiver>
using next_tail_sender_to_t =
    next_tail_operation_t<unifex::connect_result_t<T, Receiver>>;

template <typename T>
UNIFEX_CONCEPT_FRAGMENT(
    _tail_sender_traits,
    requires(const T& t)(t.sends_done) &&
        (unifex::blocking_v<T> == unifex::blocking_kind::always_inline) &&
        same_as<unifex::sender_single_value_return_type_t<T>, void>);

template <typename T>
UNIFEX_CONCEPT
_tail_sender = (
    /* unifex::is_sender_nofail_v<T> && */       // just() doesn't know it is
                                                 // nofail until a receiver is
                                                 // connected..
    (std::is_nothrow_move_constructible_v<T>)&&  //
    (std::is_nothrow_destructible_v<T>)&&        //
    (std::is_trivially_destructible_v<T>)&&      //
    UNIFEX_FRAGMENT(unifex::_tail_sender_traits, T));

template <typename T>
UNIFEX_CONCEPT
tail_sender_or_void = same_as<T, void> || _tail_sender<T>;

template <typename T>
UNIFEX_CONCEPT_FRAGMENT(
    _terminal_tail_operation_start,
    requires(T& c)(unifex::start(c)) &&
        (same_as<decltype(unifex::start(std::declval<T&>())), void>));

template <typename T>
UNIFEX_CONCEPT
_terminal_tail_operation = (_tail_operation<T>)&&  //
    UNIFEX_FRAGMENT(unifex::_terminal_tail_operation_start, T);

template <typename T, typename Receiver>
UNIFEX_CONCEPT_FRAGMENT(
    _terminal_tail_sender_operation_start,
    requires(T c, Receiver r)(unifex::connect(c, r)) &&
        (_terminal_tail_operation<connect_result_t<T, Receiver>>));

template <typename T, typename Receiver>
UNIFEX_CONCEPT
_terminal_tail_sender_to =
    UNIFEX_FRAGMENT(unifex::_terminal_tail_sender_operation_start, T, Receiver);

template <typename T>
UNIFEX_CONCEPT
_tail_receiver =                                 //
    (unifex::is_nothrow_receiver_of_v<T>)&&      //
    (std::is_nothrow_copy_constructible_v<T>)&&  //
    (std::is_nothrow_move_constructible_v<T>)&&  //
    (std::is_nothrow_copy_assignable_v<T>)&&     //
    (std::is_nothrow_move_assignable_v<T>)&&     //
    (std::is_trivially_destructible_v<T>);

struct null_tail_sender;

template <typename T, typename Receiver>
UNIFEX_CONCEPT_FRAGMENT(
    _tail_sender_to_impl,
    requires(T c, Receiver r)                              //
        (c.sends_done, unifex::connect(c, r)) &&           //
        (unifex::is_nothrow_connectable_v<T, Receiver>)&&  //
        (_tail_operation<connect_result_t<
             T,
             Receiver>>)&&  //
        (tail_sender_or_void<next_tail_sender_to_t<T, Receiver>>));

template <typename T, typename Receiver>
UNIFEX_CONCEPT
_tail_sender_to =                 //
    (_tail_sender<T>)&&           //
    (_tail_receiver<Receiver>)&&  //
    UNIFEX_FRAGMENT(unifex::_tail_sender_to_impl, T, Receiver);

template <typename T, typename Receiver, typename... ValidTailSender>
UNIFEX_CONCEPT_FRAGMENT(
    _tail_sender_recurse,
    requires(T t, Receiver r)                               //
        (t.sends_done, unifex::connect(t, r)) &&            //
        (_tail_operation<connect_result_t<T, Receiver>>)&&  //
        (one_of<next_tail_sender_to_t<T, Receiver>, ValidTailSender...>));

template <typename T, typename Receiver, typename... ValidTailSender>
UNIFEX_CONCEPT
_recursive_tail_sender_to =           //
    (_tail_sender_to<T, Receiver>)&&  //
    UNIFEX_FRAGMENT(
        unifex::_tail_sender_recurse, T, Receiver, ValidTailSender...);

template <typename T>
UNIFEX_CONCEPT
_nullable_tail_operation =  //
    (_tail_operation<T>)&&  //
    nothrow_contextually_convertible_to_bool<T>;

template <typename T, typename Receiver>
UNIFEX_CONCEPT_FRAGMENT(
    _tail_sender_nullable,
    requires(T t)(t.sends_done) &&
        (_nullable_tail_operation<unifex::connect_result_t<T, Receiver>>));

template <typename T, typename Receiver>
UNIFEX_CONCEPT
_nullable_tail_sender_to =
    UNIFEX_FRAGMENT(unifex::_tail_sender_nullable, T, Receiver);

struct tail_sender_base {
  tail_sender_base() noexcept {}
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<>>;
  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;
  static inline constexpr bool sends_done = false;

  template(typename T)                              //
      (requires derived_from<T, tail_sender_base>)  //
      friend constexpr blocking_kind
      tag_invoke(tag_t<blocking>&, const T&) noexcept {
    return blocking_kind::always_inline;
  }
};

struct tail_operation_state_base {
  tail_operation_state_base() noexcept {}
  // derived will have a stable this pointer value that can be passed to nested
  // async scopes
  tail_operation_state_base(const tail_operation_state_base&) = delete;
  tail_operation_state_base(tail_operation_state_base&&) = delete;
  tail_operation_state_base&
  operator=(const tail_operation_state_base&) = delete;
  tail_operation_state_base& operator=(tail_operation_state_base&&) = delete;

  // hide these to implement behaviour
  // this is intentionally not a virtual base class
  [[noreturn]] inline void start() noexcept { std::terminate(); }
  [[noreturn]] inline void unwind() noexcept { std::terminate(); }
};

struct null_tail_receiver {
  void set_value() noexcept {}
  void set_error(std::exception_ptr) noexcept {}
  void set_done() noexcept {}
};

struct null_tail_sender : tail_sender_base {
  // operator any_tail_sender() const noexcept { return any_tail_sender(); }
  struct op : tail_operation_state_base {
    // this is a nullable_tail_sender that always returns false to prevent
    // callers from calling start() and unwind()
    inline constexpr explicit operator bool() const noexcept { return false; }
  };
  template <typename Receiver>
  inline op connect(Receiver) noexcept {
    return {};
  }
};

template <typename... Cs>
struct _variant_tail_sender;

namespace _tail_sender_detail {
template <typename C>
struct _flatten_variant_element {
  using type = type_list<C>;
};

template <typename... Cs>
struct _flatten_variant_element<_variant_tail_sender<Cs...>> {
  using type = type_list<Cs...>;
};

template <typename... Cs>
struct _variant_or_single {
  using type = _variant_tail_sender<Cs...>;
};

template <typename C>
struct _variant_or_single<C> {
  using type = C;
};

template <>
struct _variant_or_single<> {
  using type = null_tail_sender;
};
}  // namespace _tail_sender_detail

template <typename T>
using replace_void_with_null_tail_sender =
    std::conditional_t<std::is_void_v<T>, null_tail_sender, T>;

template <typename CPO, typename Target, typename... Args>
auto result_or_null_tail_sender(CPO cpo, Target&& t, Args&&... args) {
  if constexpr (std::is_void_v<callable_result_t<CPO, Target, Args...>>) {
    cpo((Target &&) t, (Args &&) args...);
    return null_tail_sender{};
  } else {
    return cpo((Target &&) t, (Args &&) args...);
  }
}

template <typename... Cs>
using variant_tail_sender = typename concat_type_lists_unique_t<
    typename _tail_sender_detail::_flatten_variant_element<
        replace_void_with_null_tail_sender<Cs>>::type...>::
    template apply<_tail_sender_detail::_variant_or_single>::type;

// This handles the potential for recursion
struct _has_tail_sender_start_impl {
  template(typename Receiver, typename T, typename... PrevTailSenders)  //
      (requires                                                         //
       all_true<
           (!instance_of_v<_variant_tail_sender, T>),
           (!std::is_void_v<T>),
           (!same_as<null_tail_sender, T>),
           (!_recursive_tail_sender_to<T, Receiver, PrevTailSenders...>),
           (!_tail_sender_to<
               T,
               Receiver>)>)  //
      static inline constexpr bool _value() noexcept {
    static_assert(_tail_receiver<Receiver>);
    return false;
  }

  template(typename Receiver, typename T, typename... PrevTailSenders)  //
      (requires                                                         //
       (!instance_of_v<_variant_tail_sender, T>) &&
       any_true<
           (std::is_void_v<T>),
           (same_as<null_tail_sender, T>),
           (_recursive_tail_sender_to<T, Receiver, PrevTailSenders...>),
           (_tail_sender_to<
               T,
               Receiver>)>)  //
      static inline constexpr bool _value() noexcept {
    static_assert(_tail_receiver<Receiver>);
    return true;
  }

  template <typename Receiver, typename... PrevTailSenders>
  struct _variant_value_proxy {
    template <typename... Cs>
    static inline constexpr bool variant_value(_variant_tail_sender<Cs...>*) {
      return _has_tail_sender_start_impl::_value<
          Receiver,
          Cs...,
          _variant_tail_sender<Cs...>,
          PrevTailSenders...>();
    }
  };

  template(typename Receiver, typename T, typename... PrevTailSenders)  //
      (requires                                                         //
       (instance_of_v<_variant_tail_sender, T>))                        //
      static inline constexpr auto _value() noexcept
      -> decltype(_has_tail_sender_start_impl::
                      _variant_value_proxy<Receiver, PrevTailSenders...>::
                          variant_value(static_cast<T*>(nullptr))) {
    static_assert(_tail_receiver<Receiver>);
    return _has_tail_sender_start_impl::
        _variant_value_proxy<Receiver, PrevTailSenders...>::variant_value(
            static_cast<T*>(nullptr));
  }
};

template <typename T, typename Receiver, typename... PrevTailSenders>
inline constexpr bool _has_tail_sender_to = _has_tail_sender_start_impl::
    template _value<Receiver, T, PrevTailSenders...>();

template <typename T>
UNIFEX_CONCEPT
tail_receiver = (_tail_receiver<T>);

template <typename T>
UNIFEX_CONCEPT
tail_operation = (_tail_operation<T>);

template <typename T, typename Receiver>
UNIFEX_CONCEPT
tail_sender_to =                      //
    (_tail_sender_to<T, Receiver>)&&  //
    (_has_tail_sender_to<T, Receiver>);

template <typename T>
UNIFEX_CONCEPT
tail_sender = (tail_sender_to<T, null_tail_receiver>);

template <typename T, typename Receiver>
UNIFEX_CONCEPT
terminal_tail_sender_to =            //
    (tail_sender_to<T, Receiver>)&&  //
    (_terminal_tail_sender_to<T, Receiver>);

template <typename T>
UNIFEX_CONCEPT
terminal_tail_sender = (terminal_tail_sender_to<T, null_tail_receiver>);

template <typename T, typename Receiver>
UNIFEX_CONCEPT
nullable_tail_sender_to = (_nullable_tail_sender_to<T, Receiver>);

template <typename T>
UNIFEX_CONCEPT
nullable_tail_sender = (nullable_tail_sender_to<T, null_tail_receiver>);

template(typename C)     //
    (requires            //
     (_tail_sender<C>))  //
    struct maybe_tail_sender : tail_sender_base {
  maybe_tail_sender() noexcept = default;
  maybe_tail_sender(null_tail_sender) noexcept {}
  maybe_tail_sender(C c) noexcept : tail_sender_(c) {}
  template <typename Receiver>
  struct op : tail_operation_state_base {
    using op_t = connect_result_t<C, Receiver>;
    explicit op(C c, Receiver r) : op_(unifex::connect(c, r)) {}
    // operator any_tail_operation() const noexcept {
    //   return tail_sender_ ? any_tail_operation(*tail_sender_)
    //                        : any_tail_operation();
    // }
    explicit operator bool() const noexcept { return !!op_; }
    void unwind() const noexcept { op_.unwind(); }
    auto start() const noexcept { return unifex::start(op_); }
    op_t op_;
  };

  template <typename Receiver>
  op<Receiver> connect(Receiver r) {
    return {tail_sender_, r};
  }

private:
  C tail_sender_;
};

template(typename C, typename Receiver = null_tail_receiver)  //
    (requires                                                 //
     (_tail_sender<C>) &&                                     //
     (tail_receiver<Receiver>))                               //
    struct scoped_tail_sender {
  explicit scoped_tail_sender(C s, Receiver r = Receiver{}) noexcept
    : s_(s)
    , r_(r)
    , valid_(true) {}

  scoped_tail_sender(scoped_tail_sender&& other) noexcept
    : s_(other.s_)
    , r_(other.r_)
    , valid_(std::exchange(other.valid_, false)) {}

  ~scoped_tail_sender() {
    if (valid_) {
      auto op = unifex::connect(s_, r_);
      if constexpr (nullable_tail_sender_to<C, Receiver>) {
        if (!!op) {
          op.unwind();
        }
      } else {
        op.unwind();
      }
    }
  }

  C get() noexcept { return s_; }

  C release() noexcept {
    valid_ = false;
    return s_;
  }

private:
  C s_;
  Receiver r_;
  bool valid_;
};

namespace _tail {
template <typename TailFn, typename Receiver>
struct _op {
  struct type : tail_operation_state_base {
    using op_t = callable_result_t<TailFn>;
    op_t op_;
    Receiver r_;
    template <typename TailFn2, typename Receiver2>
    type(TailFn2&& t, Receiver2 r) noexcept(
        noexcept(op_t(t())) && noexcept(Receiver(r)))
      : op_(t())
      , r_(r) {}
    inline constexpr explicit operator bool() const noexcept {
      if constexpr (nothrow_contextually_convertible_to_bool<op_t>) {
        return !!op_;
      } else {
        return true;
      }
    }
    template(typename... As)            //
        (requires(sizeof...(As) == 0))  //
        void unwind(As...) noexcept {
      unifex::set_done(std::move(r_));
      op_.unwind();
    }
    template(typename... As)            //
        (requires(sizeof...(As) == 0))  //
        auto start(As&&...) noexcept {
      unifex::set_value(std::move(r_));
      if constexpr (std::is_void_v<decltype(unifex::start(op_))>) {
        unifex::start(op_);
        return;
      } else {
        return unifex::start(op_);
      }
    }
  };
};
template <typename TailFn>
struct _sender {
  struct type : tail_sender_base {
    remove_cvref_t<TailFn> t_;

    template(typename TailFn2)                                        //
        (requires                                                     //
         (std::is_constructible_v<remove_cvref_t<TailFn>, TailFn2>))  //
        type(TailFn2&& t) noexcept(
            std::is_nothrow_constructible_v<remove_cvref_t<TailFn>, TailFn2>)
      : t_(t) {}

    template(typename Receiver)        //
        (requires                      //
         (tail_receiver<Receiver>) &&  //
         (std::is_nothrow_constructible_v<
             typename _op<TailFn, Receiver>::type,
             TailFn,
             Receiver>))  //
        typename _op<TailFn, Receiver>::type connect(Receiver r) noexcept {
      return {t_, r};
    }
  };
};
struct _fn {
  template(typename TailFn)                                                 //
      (requires                                                             //
       (is_nothrow_callable_v<TailFn>) &&                                   //
       (std::is_nothrow_constructible_v<remove_cvref_t<TailFn>, TailFn>)&&  //
       (std::is_nothrow_constructible_v<
           typename _sender<TailFn>::type,
           TailFn>))  //
      typename _sender<TailFn>::type
      operator()(TailFn t) const noexcept {
    return {t};
  }
};
}  // namespace _tail

inline constexpr auto tail = _tail::_fn{};

namespace _as_tail {
template <typename Sender, typename Receiver>
struct _op {
  struct type;
};
template <typename Sender, typename Receiver>
struct _rcvr {
  struct type {
    template <typename... Vs>
    auto set_value(Vs&&...) noexcept {
      static_assert(
          is_callable_v<tag_t<unifex::set_value>, Receiver>,
          "the receiver set_value() must accept no value arguments");
      static_assert(
          is_nothrow_callable_v<tag_t<unifex::set_value>, Receiver>,
          "the receiver set_value must be noexcept");
      return unifex::set_value(std::move(*r_));
    }
    template <typename E>
    auto set_error(E&& e) noexcept {
      static_assert(
          same_as<E, std::exception_ptr>,
          "the sender must only call set_error(std::exception_ptr), or not "
          "call set_error() at all");
      return unifex::set_error(std::move(*r_), (E &&) e);
    }
    template(typename... As)    //
        (requires               //
         (sizeof...(As) == 0))  //
        auto set_done(As&&...) noexcept {
      return unifex::set_done(std::move(*r_));
    }
    Receiver* r_;
  };
};
template <typename Sender, typename Receiver>
struct _op<Sender, Receiver>::type : tail_operation_state_base {
  using rec_t = typename _rcvr<Sender, Receiver>::type;
  using op_t = connect_result_t<Sender, rec_t>;
  template <typename Sender2, typename Receiver2>
  type(Sender2&& s, Receiver2&& r) noexcept
    : r_((Receiver2 &&) r)
    , op_(unifex::connect((Sender2 &&) s, rec_t{&r_})) {}
  Receiver r_;
  op_t op_;
  void unwind() noexcept {
    if constexpr (tail_operation<op_t>) {
      op_.unwind();
    }
  }
  auto start() noexcept { return unifex::start(op_); }
};

template <typename Sender>
struct _sender {
  struct type : tail_sender_base {
    Sender s_;
    template(typename Receiver)        //
        (requires                      //
         (tail_receiver<Receiver>) &&  //
         (sender_to<
             Sender,
             typename _rcvr<remove_cvref_t<Sender>, remove_cvref_t<Receiver>>::
                 type>))  //
        typename _op<remove_cvref_t<Sender>, remove_cvref_t<Receiver>>::type
        connect(Receiver&& r) noexcept {
      return {s_, (Receiver &&) r};
    }
    friend constexpr blocking_kind tag_invoke(
        constexpr_value<tag_t<blocking>>,
        constexpr_value<const type>) noexcept {
      return blocking_v<Sender>;
    }
  };
};
struct _fn {
  template(typename TailSender)                                     //
      (requires                                                     //
       (sender<TailSender>) &&                                      //
       (blocking_v<TailSender> == blocking_kind::always_inline) &&  //
       (tail_sender<TailSender>))                                   //
      TailSender
      operator()(TailSender&& s) const noexcept {
    return (TailSender &&) s;
  }
  template(typename Sender)                                     //
      (requires                                                 //
       (sender<Sender>) &&                                      //
       (blocking_v<Sender> == blocking_kind::always_inline) &&  //
       (!tail_sender<Sender>))                                  //
      typename _sender<Sender>::type
      operator()(Sender s) const noexcept {
    return {{}, s};
  }
};
inline constexpr auto as_tail_sender = _as_tail::_fn{};
}  // namespace _as_tail

using _as_tail::as_tail_sender;
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
