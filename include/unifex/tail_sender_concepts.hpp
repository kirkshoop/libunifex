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

#include <unifex/blocking.hpp>
#include <unifex/packaged_callable.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/detail/unifex_fwd.hpp>

#include <algorithm>
#include <exception>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

//////////////////////////////////////////////////
// Tail Callable Concepts

template <typename T>
using next_tail_operation_t =
    unifex::callable_result_t<unifex::tag_t<unifex::start>, T&>;

template <typename T>
UNIFEX_CONCEPT_FRAGMENT(                                        //
    _tail_operation_impl,                                       //
    requires(T& c)                                              //
        (c.unwind(),                                            //
         unifex::start(c)) &&                                   //
        noexcept(std::declval<T>().unwind()) &&                 //
        same_as<decltype(std::declval<T>().unwind()), void> &&  //
        unifex::is_nothrow_callable_v<unifex::tag_t<unifex::start>, T&>);

template <typename T>
UNIFEX_CONCEPT _tail_operation =                                      //
    (                                                                 //
        /* just()'s operation state needs to disable copy and move*/  //
        /* (!std::is_copy_constructible_v<T>) && */                   //
        /* (!std::is_move_constructible_v<T>) && */                   //
        /* (!std::is_copy_assignable_v<T>) && */                      //
        /* (!std::is_move_assignable_v<T>) && */                      //
        (std::is_nothrow_destructible_v<T>)&&                         //
        /*(std::is_trivially_destructible_v<T>)&& */  // variant operation
                                                      // cannot be trivially
                                                      // destructible
        UNIFEX_FRAGMENT(_tail_operation_impl, T)      //
    );

template <typename T, typename Receiver>
using next_tail_sender_to_t =
    next_tail_operation_t<unifex::connect_result_t<T, Receiver>>;

template <typename T>
UNIFEX_CONCEPT_FRAGMENT(   //
    _tail_sender_traits,   //
    requires(T t)          //
        (t.sends_done) &&  //
        same_as<unifex::sender_single_value_return_type_t<T>, void>);

template <typename T>
UNIFEX_CONCEPT _tail_sender =                   //
    (                                           //
        /* unifex::is_sender_nofail_v<T> && */  // just() doesn't know it is
                                                // nofail until a receiver is
                                                // connected..
        /*(unifex::blocking(*((T*)0)) == */     //
        /* unifex::blocking_kind::always_inline) && */  // I don't know how to
                                                        // make constexpr
                                                        // transitive to members
                                                        // - `just() | then()`
        (std::is_nothrow_copy_constructible_v<T>)&&     //
        (std::is_nothrow_move_constructible_v<T>)&&     //
        (std::is_nothrow_copy_assignable_v<T>)&&        //
        (std::is_nothrow_move_assignable_v<T>)&&        //
        (std::is_nothrow_destructible_v<T>)&&           //
        /* (std::is_trivially_destructible_v<T>)&& */   // variant_sender cannot
                                                        // be trivially
                                                        // destructible
        UNIFEX_FRAGMENT(unifex::_tail_sender_traits, T));

template <typename T>
UNIFEX_CONCEPT tail_sender_or_void = same_as<T, void> || _tail_sender<T>;

template <typename T>
UNIFEX_CONCEPT_FRAGMENT(             //
    _terminal_tail_operation_start,  //
    requires(T& c)                   //
        (unifex::start(c)) &&        //
        (same_as<decltype(unifex::start(std::declval<T&>())), void>));

template <typename T>
UNIFEX_CONCEPT _terminal_tail_operation =  //
    (_tail_operation<T>)&&                 //
    UNIFEX_FRAGMENT(unifex::_terminal_tail_operation_start, T);

template <typename T, typename Receiver>
UNIFEX_CONCEPT_FRAGMENT(                    //
    _terminal_tail_sender_operation_start,  //
    requires(T c, Receiver r)               //
        (unifex::connect(c, r)) &&          //
        (_terminal_tail_operation<connect_result_t<T, Receiver>>));

template <typename T, typename Receiver>
UNIFEX_CONCEPT _terminal_tail_sender_to =  //
    UNIFEX_FRAGMENT(unifex::_terminal_tail_sender_operation_start, T, Receiver);

template <typename T>
UNIFEX_CONCEPT_FRAGMENT(                //
    _tail_receiver_impl,                //
    requires(T c)                       //
    (unifex::set_done(std::move(c))));  //

template <typename T>
UNIFEX_CONCEPT _tail_receiver =
    (unifex::receiver<T> &&
     unifex::is_nothrow_callable_v<unifex::tag_t<unifex::set_value>, T> &&
     std::is_nothrow_copy_constructible_v<T> &&
     std::is_nothrow_move_constructible_v<T> &&
     std::is_nothrow_copy_assignable_v<T> &&
     std::is_nothrow_move_assignable_v<T> &&
     std::is_trivially_destructible_v<T> &&  //
     UNIFEX_FRAGMENT(_tail_receiver_impl, T));

struct null_tail_sender;

template <typename T, typename Receiver>
UNIFEX_CONCEPT_FRAGMENT(                                             //
    _tail_sender_to_impl,                                            //
    requires(T c, Receiver r)                                        //
        (c.sends_done,                                               //
         unifex::connect(c, r)) &&                                   //
        (unifex::is_nothrow_connectable_v<T, Receiver>)&&            //
        (tail_sender_or_void<next_tail_sender_to_t<T, Receiver>>)&&  //
        (same_as<                                                    //
            unifex::sender_single_value_return_type_t<T>,            //
            void>));

template <typename T, typename Receiver>
UNIFEX_CONCEPT _tail_sender_to =   //
    ((_tail_sender<T>)&&           //
     (_tail_receiver<Receiver>)&&  //
     UNIFEX_FRAGMENT(unifex::_tail_sender_to_impl, T, Receiver));

template <typename T, typename Receiver, typename... ValidTailSender>
UNIFEX_CONCEPT_FRAGMENT(                                    //
    _tail_sender_recurse,                                   //
    requires(T t, Receiver r)                               //
        (t.sends_done,                                      //
         unifex::connect(t, r)) &&                          //
        (_tail_operation<connect_result_t<T, Receiver>>)&&  //
        (one_of<next_tail_sender_to_t<T, Receiver>, ValidTailSender...>));

template <typename T, typename Receiver, typename... ValidTailSender>
UNIFEX_CONCEPT _recursive_tail_sender_to =  //
    (_tail_sender_to<T, Receiver>)&&        //
    UNIFEX_FRAGMENT(
        unifex::_tail_sender_recurse, T, Receiver, ValidTailSender...);

template <typename T>
UNIFEX_CONCEPT _nullable_tail_operation =  //
    (_tail_operation<T>)&&                 //
    nothrow_contextually_convertible_to_bool<T>;

template <typename T, typename Receiver>
UNIFEX_CONCEPT_FRAGMENT(    //
    _tail_sender_nullable,  //
    requires(T t)           //
        (t.sends_done) &&   //
        (_nullable_tail_operation<unifex::connect_result_t<T, Receiver>>));

template <typename T, typename Receiver>
UNIFEX_CONCEPT _nullable_tail_sender_to =  //
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

  friend constexpr blocking_kind
  tag_invoke(tag_t<blocking>, const tail_sender_base&) noexcept {
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
  template(typename Receiver, typename T, typename... PrevTailSenders)    //
      (requires                                                           //
       (!std::is_void_v<T>) AND                                           //
       (!same_as<null_tail_sender, T>) AND                                //
       (!_recursive_tail_sender_to<T, Receiver, PrevTailSenders...>) AND  //
       (!_tail_sender<T>) AND                                             //
       (!instance_of_v<_variant_tail_sender, T>))                         //
      static inline constexpr bool _value() noexcept {
    return false;
  }  // namespace unifex

  template(typename Receiver, typename T, typename... PrevTailSenders)    //
      (requires                                                           //
       (!std::is_void_v<T>) AND                                           //
       (!same_as<null_tail_sender, T>) AND                                //
       (!_recursive_tail_sender_to<T, Receiver, PrevTailSenders...>) AND  //
       (_tail_sender<T>) AND                                              //
       (!instance_of_v<_variant_tail_sender, T>))                         //
      static inline constexpr bool _value() noexcept {
    return _has_tail_sender_start_impl::_value<
        Receiver,
        next_tail_sender_to_t<T, Receiver>,
        T,
        PrevTailSenders...>();
  }

  template(typename Receiver, typename T, typename... PrevTailSenders)    //
      (requires                                                           //
       (std::is_void_v<T>) AND                                            //
       (!same_as<null_tail_sender, T>) AND                                //
       (!_recursive_tail_sender_to<T, Receiver, PrevTailSenders...>) AND  //
       (!_tail_sender<T>) AND                                             //
       (!instance_of_v<_variant_tail_sender, T>))                         //
      static inline constexpr bool _value() noexcept {
    return true;
  }

  template(typename Receiver, typename T, typename... PrevTailSenders)    //
      (requires                                                           //
       (!std::is_void_v<T>) AND                                           //
       (same_as<null_tail_sender, remove_cvref_t<T>>) AND                 //
       (!_recursive_tail_sender_to<T, Receiver, PrevTailSenders...>) AND  //
       (_tail_sender<T>) AND                                              //
       (!instance_of_v<_variant_tail_sender, T>))                         //
      static inline constexpr bool _value() noexcept {
    return true;
  }

  template(
      typename Receiver,
      typename T,
      typename... PrevTailSenders)                                       //
      (requires                                                          //
       (!std::is_void_v<T>) AND                                          //
       (!same_as<null_tail_sender, T>) AND                               //
       (_recursive_tail_sender_to<T, Receiver, PrevTailSenders...>) AND  //
       (!_tail_sender<T>) AND                                            //
       (!instance_of_v<_variant_tail_sender, T>))                        //
      static inline constexpr bool _value() noexcept {
    return true;
  }

  template(
      typename Receiver,
      typename T,
      typename... PrevTailSenders)                                       //
      (requires                                                          //
       (!std::is_void_v<T>) AND                                          //
       (!same_as<null_tail_sender, T>) AND                               //
       (_recursive_tail_sender_to<T, Receiver, PrevTailSenders...>) AND  //
       (_tail_sender<T>) AND                                             //
       (!instance_of_v<_variant_tail_sender, T>))                        //
      static inline constexpr bool _value() noexcept {
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

  template(
      typename Receiver,
      typename T,
      typename... PrevTailSenders)                                        //
      (requires                                                           //
       (!std::is_void_v<T>) AND                                           //
       (!same_as<null_tail_sender, T>) AND                                //
       (!_recursive_tail_sender_to<T, Receiver, PrevTailSenders...>) AND  //
       (_tail_sender<T>) AND                                              //
       (instance_of_v<_variant_tail_sender, T>))                          //
      static inline constexpr auto _value() noexcept
      -> decltype(_has_tail_sender_start_impl::
                      _variant_value_proxy<Receiver, PrevTailSenders...>::
                          variant_value(static_cast<T*>(nullptr))) {
    return _has_tail_sender_start_impl::
        _variant_value_proxy<Receiver, PrevTailSenders...>::variant_value(
            static_cast<T*>(nullptr));
  }
};

template <typename T, typename Receiver, typename... PrevTailSenders>
inline constexpr bool _has_tail_sender_to = _has_tail_sender_start_impl::
    template _value<Receiver, T, PrevTailSenders...>();

template <typename T>
UNIFEX_CONCEPT tail_receiver =  //
    (_tail_receiver<T>);

template <typename T>
UNIFEX_CONCEPT tail_operation =  //
    (_tail_operation<T>);

template <typename T, typename Receiver>
UNIFEX_CONCEPT tail_sender_to =       //
    (_tail_sender_to<T, Receiver>)&&  //
    (_has_tail_sender_to<T, Receiver>);

template <typename T, typename Receiver>
UNIFEX_CONCEPT terminal_tail_sender_to =  //
    (tail_sender_to<T, Receiver>)&&       //
    (_terminal_tail_sender_to<T, Receiver>);

template <typename T, typename Receiver>
UNIFEX_CONCEPT nullable_tail_sender_to =  //
    (_nullable_tail_sender_to<T, Receiver>);

template(typename C, typename Receiver)  //
    (requires                            //
     (!std::is_void_v<C>) &&             //
     (tail_receiver<Receiver>)&&         //
     (_tail_sender<C>))                  //
    auto _resume_until_nullable(C c, Receiver r) {
  if constexpr (nullable_tail_sender_to<C, Receiver>) {
    return c;
  } else {
    // restrict scope of op
    auto c2 = [&]() {
      auto op = unifex::connect(std::move(c), r);
      return unifex::start(op);
    }();
    return _resume_until_nullable(std::move(c2), std::move(r));
  }
}

template(typename C, typename Receiver)  //
    (requires                            //
     (!std::is_void_v<C>) &&             //
     (tail_receiver<Receiver>)&&         //
     (_tail_sender<C>))                  //
    auto _invoke_until_nullable(C c, Receiver r) {
  if constexpr (nullable_tail_sender_to<C, Receiver>) {
    return c;
  } else if constexpr (_terminal_tail_sender_to<C, Receiver>) {
    // restrict scope of op
    {
      auto op = unifex::connect(std::move(c), std::move(r));
      unifex::start(op);
    }
    return null_tail_sender{};
  } else {
    auto op = unifex::connect(std::move(c), r);
    return _invoke_until_nullable(unifex::start(op), std::move(r));
  }
}

template(typename C, typename Receiver, typename... Prev)  //
    (requires(!std::is_void_v<C>))                         //
    auto _invoke_sequential(C c, Receiver r, type_list<Prev...>) {
  static_assert(
      _tail_sender<C>, "_invoke_sequential: must be called with a tail_sender");
  if constexpr (_terminal_tail_sender_to<C, Receiver>) {
    if constexpr (nullable_tail_sender_to<C, Receiver>) {
      return c;
    } else {
      // restrict scope of op
      {
        auto op = unifex::connect(std::move(c), std::move(r));
        unifex::start(op);
      }
      return null_tail_sender{};
    }
  } else {
    using next_t = next_tail_sender_to_t<C, Receiver>;
    if constexpr (std::is_void_v<next_t>) {
      return null_tail_sender{};
    } else {
      using opt_t = std::optional<next_t>;
      // restrict scope of op
      opt_t next = [&]() -> opt_t {
        auto op = unifex::connect(std::move(c), r);
        if constexpr (nullable_tail_sender_to<C, Receiver>) {
          if (!op) {
            return {std::nullopt};
          }
        }
        return {unifex::start(op)};
      }();

      if constexpr (one_of<next_t, C, Prev...>) {
        static_assert(
            (nullable_tail_sender_to<C, Receiver> ||
             (nullable_tail_sender_to<Prev, Receiver> || ...)),
            "At least one tail_sender in a cycle must be nullable to avoid "
            "entering an infinite loop");
        using result_type = variant_tail_sender<
            null_tail_sender,
            decltype(_invoke_until_nullable(*next, std::move(r)))>;
        if (!next) {
          return result_type{null_tail_sender{}};
        }
        return result_type{_invoke_until_nullable(*next, std::move(r))};
      } else {
        using result_type = variant_tail_sender<
            null_tail_sender,
            next_t,
            decltype(_invoke_sequential(*next, r, type_list<C, Prev...>{}))>;
        if constexpr (nullable_tail_sender_to<C, Receiver>) {
          if (!next) {
            return result_type{null_tail_sender{}};
          }
        }
        if constexpr (same_as<result_type, next_t>) {
          // Let the loop in resume_tail_sender() handle checking the boolean.
          return result_type{*next};
        } else {
          return result_type{
              _invoke_sequential(*next, std::move(r), type_list<C, Prev...>{})};
        }
      }
    }
  }
}

template(typename C, typename Receiver)  //
    (requires                            //
     (!std::is_void_v<C>) &&             //
     (tail_receiver<Receiver>))          //
    auto _invoke_sequential(C c, Receiver r) {
  static_assert(
      _tail_sender<C>, "_invoke_sequential: must be called with a tail_sender");
  return _invoke_sequential(c, r, type_list<>{});
}

template(typename C, typename Receiver = null_tail_receiver)  //
    (requires                                                 //
     (!std::is_void_v<C>) &&                                  //
     (tail_receiver<Receiver>)&&                              //
     (_tail_sender<C>))                                       //
    void resume_tail_sender(C c, Receiver r = Receiver{}) {
  static_assert(
      nullable_tail_sender_to<decltype(_invoke_sequential(c, r)), Receiver>,
      "resume_tail_sender: _invoke_sequential must return a "
      "nullable_tail_sender");
  auto c2 = _invoke_sequential(c, r);
  for (;;) {
    auto op = unifex::connect(c2, r);
    if (!op) {
      break;
    }
    if constexpr (_terminal_tail_sender_to<decltype(c2), Receiver>) {
      unifex::start(op);
      break;
    } else {
      c2 = _invoke_sequential(unifex::start(op), r, type_list<>{});
    }
  }
}

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

struct _first_defaultable_tail_sender {
  template <typename C, typename... Rest>
  struct _type_next;

  template(typename C, typename... Rest)                //
      (requires                                         //
       (std::is_nothrow_default_constructible_v<C>) &&  //
       (all_true<_tail_sender<Rest>...>))               //
      static inline constexpr C _type();

  template(typename Receiver, typename C, typename... Rest)  //
      (requires                                              //
       (!std::is_nothrow_default_constructible_v<C>) &&      //
       (all_true<_tail_sender<Rest>...>))                    //
      static inline constexpr auto _type() ->
      typename _type_next<C, Rest...>::type;

  template <typename C, typename... Rest>
  struct _type_next {
    using type = decltype(_type<Rest...>());
  };

  template <typename... Cs>
  using type = decltype(_type<Cs...>());
};

template <typename... Cs>
using first_defaultable_tail_sender_t =
    typename _first_defaultable_tail_sender::type<Cs...>;

template <typename... Cs>
struct _variant_tail_sender : tail_sender_base {
  static_assert(sizeof...(Cs) >= 2);
  static_assert(types_are_unique_v<Cs...>);

  ~_variant_tail_sender() noexcept { reset(); }

  _variant_tail_sender() noexcept : tag(-1) {}

  template(typename C)                                          //
      (requires                                                 //
       (_tail_sender<C>) &&                                     //
       (one_of<C, replace_void_with_null_tail_sender<Cs>...>))  //
      _variant_tail_sender(C c) noexcept
    : tag(-1) {
    emplace(std::move(c));
  }

  _variant_tail_sender(const _variant_tail_sender& o) noexcept {
    o.visit([this, &o](const auto& v) {
      using v_t = remove_cvref_t<decltype(v)>;
      state.template construct<v_t>(v);
      tag = o.tag;
    });
  }
  _variant_tail_sender& operator=(const _variant_tail_sender& o) noexcept {
    reset();
    o.visit([this, &o](const auto& v) {
      using v_t = remove_cvref_t<decltype(v)>;
      state.template construct<v_t>(v);
      tag = o.tag;
    });
    return *this;
  }

  _variant_tail_sender(_variant_tail_sender&& o) noexcept {
    o.visit([this, &o](auto&& v) {
      using v_t = remove_cvref_t<decltype(v)>;
      state.template construct<v_t>(std::move(v));
      v.~v_t();
      tag = std::exchange(o.tag, -1);
    });
  }
  _variant_tail_sender& operator=(_variant_tail_sender&& o) noexcept {
    reset();
    o.visit([this, &o](auto&& v) {
      using v_t = remove_cvref_t<decltype(v)>;
      state.template construct<v_t>(std::move(v));
      v.~v_t();
      tag = std::exchange(o.tag, -1);
    });
    return *this;
  }

  template(typename... OtherCs)                 //
      (requires                                 //
       (all_true<_tail_sender<OtherCs>...>) &&  //
       (all_true<one_of<
            replace_void_with_null_tail_sender<OtherCs>,
            replace_void_with_null_tail_sender<Cs>...>...>))  //
      _variant_tail_sender(_variant_tail_sender<OtherCs...> c) noexcept {
    c.visit([this](auto other_c) noexcept {
      *this = _variant_tail_sender(other_c);
    });
  }

  template(typename C)       //
      (requires              //
       (_tail_sender<C>) &&  //
       (one_of<C, Cs...>))   //
      void emplace(C c) noexcept {
    reset();
    state.template construct<C>(std::move(c));
    tag = index_of_v<C, Cs...>;
  }

  void reset() noexcept {
    if (tag >= 0 && tag < std::ptrdiff_t(sizeof...(Cs))) {
      visit([this](auto& v) {
        using v_t = remove_cvref_t<decltype(v)>;
        state.template destruct<v_t>();
        tag = -1;
      });
    }
  }

  template <typename... Operations>
  struct op : tail_operation_state_base {
    static_assert(sizeof...(Operations) >= 2);

    ~op() noexcept { reset(); }
    op() : tag(-1) {}

    template(typename Sender, typename Receiver)  //
        (requires                                 //
         (_tail_sender<Sender>) &&                //
         (one_of<
             unifex::connect_result_t<Sender, Receiver>,
             Operations...>))  //
        op(Sender c, Receiver r) noexcept
      : tag(-1) {
      emplace(std::move(c), std::move(r));
    }

    template(typename Sender, typename Receiver)  //
        (requires                                 //
         (_tail_sender<Sender>) &&                //
         (one_of<
             unifex::connect_result_t<Sender, Receiver>,
             Operations...>))  //
        void emplace(Sender c, Receiver r) noexcept {
      reset();
      using op_t = unifex::connect_result_t<Sender, Receiver>;
      state.template construct<op_t>(
          packaged_callable{unifex::connect, std::move(c), std::move(r)});
      tag =
          index_of_v<unifex::connect_result_t<Sender, Receiver>, Operations...>;
    }

    void reset() noexcept {
      if (tag >= 0 && tag < std::ptrdiff_t(sizeof...(Cs))) {
        visit([this](auto& v) {
          using v_t = remove_cvref_t<decltype(v)>;
          state.template destruct<v_t>();
          tag = -1;
        });
      }
    }

    explicit operator bool() const noexcept {
      return visit([](const auto& op) {
        if constexpr (nothrow_contextually_convertible_to_bool<decltype(op)>) {
          return !!op;
        } else {
          return true;
        }
      });
    }

    void unwind() noexcept {
      visit([](auto& op) { op.unwind(); });
    }

    using start_result_tail_sender = variant_tail_sender<
        null_tail_sender,
        Cs...,
        decltype(unifex::start(std::declval<Operations&>()))...>;
    start_result_tail_sender start() noexcept {
      return {visit([](auto& op) -> start_result_tail_sender {
        if constexpr (_nullable_tail_operation<decltype(op)>) {
          if (!op) {
            return {null_tail_sender{}};
          }
        }
        if constexpr (std::is_void_v<unifex::callable_result_t<
                          unifex::tag_t<unifex::start>,
                          decltype(op)>>) {
          unifex::start(op);
          return {null_tail_sender{}};
        } else {
          return {unifex::start(op)};
        }
      })};
    }

  private:
    template <typename F>
    auto visit(F f) noexcept {
      return visit_impl(std::move(f), std::index_sequence_for<Operations...>{});
    }

    template <typename F>
    auto visit(F f) const noexcept {
      return visit_impl(std::move(f), std::index_sequence_for<Operations...>{});
    }

    template <typename F, std::size_t Idx, std::size_t... Indices>
    auto visit_impl(F f, std::index_sequence<Idx, Indices...>) noexcept {
      using T = nth_type_t<Idx, Operations...>;
      if constexpr (sizeof...(Indices) == 0) {
        auto& op = state.template get<T>();
        return f(op);
      } else if (tag == Idx) {
        auto& op = state.template get<T>();
        return f(op);
      } else {
        return visit_impl(std::move(f), std::index_sequence<Indices...>{});
      }
    }

    template <typename F, std::size_t Idx, std::size_t... Indices>
    auto visit_impl(F f, std::index_sequence<Idx, Indices...>) const noexcept {
      using T = nth_type_t<Idx, Operations...>;
      if constexpr (sizeof...(Indices) == 0) {
        const auto& op = state.template get<T>();
        return f(op);
      } else if (tag == Idx) {
        const auto& op = state.template get<T>();
        return f(op);
      } else {
        return visit_impl(std::move(f), std::index_sequence<Indices...>{});
      }
    }

    std::ptrdiff_t tag;
    manual_lifetime_union<Operations...> state;
  };

  template(typename Receiver)      //
      (requires                    //
       (tail_receiver<Receiver>))  //
      op<unifex::connect_result_t<Cs, Receiver>...> connect(
          Receiver r) noexcept {
    return visit(
        [&](auto c) noexcept -> op<unifex::connect_result_t<Cs, Receiver>...> {
          static_assert(_tail_sender<decltype(c)>);
          return {c, r};
        });
  }

private:
  template <typename... OtherCs>
  friend struct _variant_tail_sender;

  template <typename F>
  auto visit(F f) const noexcept {
    return visit_impl(std::move(f), std::index_sequence_for<Cs...>{});
  }

  template <typename F, std::size_t Idx, std::size_t... Indices>
  auto visit_impl(F f, std::index_sequence<Idx, Indices...>) const noexcept {
    if (tag < 0 || tag > std::ptrdiff_t(sizeof...(Cs))) {
      std::terminate();
    }
    using T = nth_type_t<Idx, replace_void_with_null_tail_sender<Cs>...>;
    if constexpr (sizeof...(Indices) == 0) {
      return f(state.template get<T>());
    } else if (tag == Idx) {
      return f(state.template get<T>());
    } else {
      return visit_impl(std::move(f), std::index_sequence<Indices...>{});
    }
  }

  std::ptrdiff_t tag;
  manual_lifetime_union<replace_void_with_null_tail_sender<Cs>...> state;
};

inline null_tail_sender resume_tail_senders_until_one_remaining() noexcept {
  return {};
}

template(typename Receiver, typename C)  //
    (requires                            //
     _tail_sender<C>)                    //
    C resume_tail_senders_until_one_remaining(Receiver, C c) noexcept {
  return c;
}

template(typename... Cs, std::size_t... Is, typename Receiver)  //
    (requires                                                   //
     all_true<_tail_sender<Cs>...>)                             //
    UNIFEX_ALWAYS_INLINE auto _resume_tail_senders_until_one_remaining(
        std::index_sequence<Is...>, Receiver r, Cs... cs) noexcept {
  std::size_t remaining = sizeof...(cs);
  auto invoke_one = [&](auto& s) noexcept {
    auto op = unifex::connect(s, r);
    using one_result_type = variant_tail_sender<
        unifex::null_tail_sender,
        unifex::callable_result_t<unifex::tag_t<unifex::start>, decltype(op)&>>;
    if constexpr (nullable_tail_sender_to<decltype(s), Receiver>) {
      if (!op) {
        --remaining;
        return one_result_type{unifex::null_tail_sender{}};
      }
    }
    return one_result_type{unifex::start(op)};
  };

  using result_type =
      variant_tail_sender<decltype(_invoke_sequential(invoke_one(cs), r))...>;
  result_type result;

  auto cs2_tuple = std::make_tuple(_invoke_sequential(invoke_one(cs), r)...);

  while (true) {
    remaining = sizeof...(cs);
    ((remaining > 1
          ? (void)(result = std::get<Is>(cs2_tuple) = _invoke_sequential(invoke_one(std::get<Is>(cs2_tuple)), r))
          : (void)(result = std::get<Is>(cs2_tuple))),
     ...);

    if (remaining <= 1) {
      return result;
    }
  }
}

template(typename Receiver, typename... Cs)        //
    (requires                                      //
     (all_true<tail_sender_to<Cs, Receiver>...>))  //
    auto resume_tail_senders_until_one_remaining(
        Receiver r, Cs... cs) noexcept {
  return _resume_tail_senders_until_one_remaining(
      std::index_sequence_for<Cs...>{}, r, cs...);
}

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

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
