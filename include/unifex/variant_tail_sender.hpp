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

#include <unifex/tag_invoke.hpp>
#include <unifex/tail_sender_concepts.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>

#include <algorithm>
#include <exception>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

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
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
