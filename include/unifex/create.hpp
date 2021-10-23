/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <unifex/manual_lifetime.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/type_traits.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _create {

template <typename Receiver, typename Fn, typename... Context>
struct _op {
  struct type;
  struct _receiver {
    explicit _receiver(type* op) noexcept(
        std::is_nothrow_move_constructible_v<Receiver>)
      : op_(op) {}

    template(typename... Ts)                     //
        (requires receiver_of<Receiver, Ts...>)  //
        void set_value(Ts&&... ts) noexcept(
            is_nothrow_receiver_of_v<Receiver, Ts...>) {
      unifex::set_value(std::move(get_receiver()), (Ts &&) ts...);
    }

    template(typename Error)                  //
        (requires receiver<Receiver, Error>)  //
        void set_error(Error&& error) noexcept {
      unifex::set_error(std::move(get_receiver()), (Error &&) error);
    }

    void set_done() noexcept {
      unifex::set_done(std::move(get_receiver()));
    }

  private:
    Receiver& get_receiver() const { return op_->get_receiver(); }
    // Forward other receiver queries
    template(typename CPO)                          //
        (requires is_receiver_query_cpo_v<CPO> AND  //
             is_callable_v<CPO, const Receiver&>)   //
        friend auto tag_invoke(CPO cpo, const _receiver& self) noexcept(
            is_nothrow_callable_v<CPO, const Receiver&>)
            -> callable_result_t<CPO, const Receiver&> {
      return std::move(cpo)(self.get_receiver());
    }
    type* op_;
  };
  struct type {
    using context_t = std::tuple<Context...>;
    using state_t = callable_result_t<Fn, _receiver&, Context&...>;

    ~type() {
      if (started_) {
        state_.destruct();
      }
    }
    explicit type(Receiver rec, Fn fn, context_t ctx) noexcept(
        std::is_nothrow_move_constructible_v<Fn>&&
            std::is_nothrow_move_constructible_v<context_t>)
      : receiver_(std::move(rec))
      , fn_(std::move(fn))
      , ctx_(std::move(ctx))
      , state_()
      , rec_(this)
      , started_(false) {}

  private:
    friend struct _receiver;
    Receiver& get_receiver() { return receiver_; }
    friend void tag_invoke(tag_t<start>, type& self) noexcept try {
      std::apply(
          [&](auto&... ctx) {
            self.state_.construct_with(
                [&]() noexcept { return self.fn_(self.rec_, ctx...); });
          },
          self.ctx_);
      self.started_ = true;
    } catch (...) {
      unifex::set_error(std::move(self.rec_), std::current_exception());
    }

    UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;
    UNIFEX_NO_UNIQUE_ADDRESS Fn fn_;
    UNIFEX_NO_UNIQUE_ADDRESS context_t ctx_;
    UNIFEX_NO_UNIQUE_ADDRESS manual_lifetime<state_t> state_;
    _receiver rec_;
    bool started_;
  };
};

template <typename Receiver, typename Fn, typename... Context>
using _operation = typename _op<Receiver, Fn, Context...>::type;

template <typename Fn, typename... Context>
struct _snd {
  struct type {
    using context_t = std::tuple<Context...>;

    template <
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
    using value_types = typename Fn::template value_types<Variant, Tuple>;

    template <template <typename...> class Variant>
    using error_types = typename Fn::template error_types<Variant>;

    static constexpr bool sends_done = Fn::sends_done;

    template(typename Self, typename Receiver)                         //
        (requires derived_from<remove_cvref_t<Self>, type> AND         //
             receiver<Receiver> AND                                    //
                 constructible_from<Fn, member_t<Self, Fn>> AND        //
         (constructible_from<Context, member_t<Self, Context>>&&...))  //
        friend _operation<
            remove_cvref_t<Receiver>,
            Fn,
            Context...>                                          //
        tag_invoke(tag_t<connect>, Self&& self, Receiver&& rec)  //
        noexcept(std::is_nothrow_constructible_v<
                 _operation<Receiver, Fn, Context...>,
                 Receiver,
                 member_t<Self, Fn>,
                 member_t<Self, context_t>>) {
      return _operation<remove_cvref_t<Receiver>, Fn, Context...>{
          (Receiver &&) rec, ((Self &&) self).fn_, ((Self &&) self).ctx_};
    }

    explicit type(Fn fn, Context... ctx)
      : fn_((Fn &&) fn)
      , ctx_((Context &&) ctx...) {}
    UNIFEX_NO_UNIQUE_ADDRESS Fn fn_;
    UNIFEX_NO_UNIQUE_ADDRESS context_t ctx_;
  };
};

template <typename Fn, typename... Context>
using _sender = typename _snd<std::decay_t<Fn>, std::decay_t<Context>...>::type;

template <typename T>
T void_cast(void* pv) noexcept {
  return static_cast<T&&>(*static_cast<std::add_pointer_t<T>>(pv));
}

namespace _cpo {
struct _fn {
  template(typename Fn, typename... Context)                             //
      (requires move_constructible<Fn> AND                               //
       (move_constructible<Context>&&...) AND                            //
           constructible_from<_sender<Fn, Context...>, Fn, Context...>)  //
      _sender<Fn, Context...>
      operator()(Fn fn, Context&&... ctx) const
      noexcept(std::is_nothrow_constructible_v<
               _sender<Fn, Context...>,
               Fn,
               Context...>) {
    return _sender<Fn, Context...>{(Fn &&) fn, (Context &&) ctx...};
  }
};
template <typename... ValueTypes>
struct _fn_simple {
  template <typename Fn>
  struct creator {
      template <
          template <typename...>
          class Variant,
          template <typename...>
          class Tuple>
      using value_types = Variant<Tuple<ValueTypes...>>;
    
      template <template <typename...> class Variant>
      using error_types = Variant<std::exception_ptr>;
    
      static inline constexpr bool sends_done = true;

      template(typename... Args)//
        (requires callable<Fn&, Args...>) //
      callable_result_t<Fn&, Args...> operator()(Args&&... args) //
        noexcept(is_nothrow_callable_v<Fn&, Args...>) { //
        return fn_((Args&&) args...);
      }

      Fn fn_;
  };

  template(typename Fn, typename... Context)  //
      (requires move_constructible<Fn> AND    //
       (move_constructible<Context>&&...))    //
      _sender<creator<Fn>, Context...>
      operator()(Fn fn, Context&&... ctx) const
      noexcept(std::is_nothrow_constructible_v<
               _sender<creator<Fn>, Context...>,
               creator<Fn>,
               Context...>) {
    return _sender<creator<Fn>, Context...>{
        creator<Fn>{(Fn &&) fn}, (Context &&) ctx...};
  }
};
}  // namespace _cpo
}  // namespace _create

/**
 * \fn auto create(auto fn, auto... ctx)
 * \brief A utility for building a sender-based API.
 *
 * \em Example:
 * \code
 *  // A void-returning C-style async API that accepts a context and a
 * continuation: using callback_t = void(void* context, int result); void
 * old_c_style_api(int a, int b, void* context, callback_t* callback_fn);
 *
 *  // A sender-based async API implemented in terms of the C-style API (using
 * C++20):
 *   unifex::typed_sender auto new_sender_api(int a, int b) {
 *     return unifex::create(unifex::simple_create<int>{
 *       [](auto& rec, int& a, int& b) noexcept {
 *         old_c_style_api(a, b, &rec, +[](void* context, int result) {
 *           unifex::set_value(unifex::void_cast<decltype(rec)>(context),
 * result);
 *         });
 *       }}, a, b);
 *   }
 * \endcode
 * \param[in] fn A callable object. This function object provides the \c
 * sender_traits to be used on the produced sender. This function object accepts
 * an lvalue reference to an object that satisfies the \c unifex::receiver<>
 * concept and lvalue references to each context parameter provided to \c
 * create(). This function object returns a state object. This state object will
 * be stored in a stable location and is allowed to be no-move no-copy. This
 * function object or the constructor of the returned state object, could
 * dispatch to a C-style callback (see example).
 * \param[in] ctx... optional
 * extra data to be passed to \c fn. E.g., \c fn is a
 * lambda that accepts <tt>(auto& rec, decltype(ctx)&...)</tt>.
 * \return A sender that, when connected and started, dispatches to the wrapped
 * C-style API with the callback of your choosing. The receiver passed to \c fn
 * wraps the receiver passed to \c connect . Your callback must "complete" the
 * receiver passed to \c fn , which will complete the receiver passed to
 * \c connect in turn.
 */
inline constexpr _create::_cpo::_fn create{};

/**
 * \fn auto create_simple<ValueTypes...>(auto fn, auto... ctx)
 * \brief A utility for building a sender-based API.
 *
 * \em Example:
 * \code
 *  // A void-returning C-style async API that accepts a context and a
 * continuation: using callback_t = void(void* context, int result); void
 * old_c_style_api(int a, int b, void* context, callback_t* callback_fn);
 *
 *  // A sender-based async API implemented in terms of the C-style API (using
 * C++20):
 *   unifex::typed_sender auto new_sender_api(int a, int b) {
 *     return unifex::create_simple<int>([a, b](auto& rec) noexcept {
 *       old_c_style_api(a, b, &rec, +[](void* context, int result) {
 *         unifex::set_value(unifex::void_cast<decltype(rec)>(context), result);
 *       });
 *     });
 *   }
 * \endcode
 * \param[in] fn A void-returning callable that accepts an lvalue reference to
 * an object that satisfies the \c unifex::receiver<> concept and lvalue
 * references to each context parameter provided to \c create_simple(). This
 * function should dispatch to a C-style callback (see example). The receiver
 * object also has a method <tt>std::tuple<ctx...> context()</tt> that provides
 * access to the arguments, after the lambda, passed to \c create_simple()
 * \param[in] ctx... optional extra data to be bundled with the receiver passed
 * to \c fn. E.g., \c fn is a lambda that accepts <tt>(auto& rec,
 * decltype(ctx)&...)</tt>.
 * \return A sender that, when connected and
 * started, dispatches to the wrapped C-style API with the callback of your
 * choosing. The receiver passed to \c fn wraps the receiver passed to \c
 * connect . Your callback must "complete" the receiver passed to \c fn , which
 * will complete the receiver passed to \c connect in turn.
 */
template <typename... ValueTypes>
inline constexpr _create::_cpo::_fn_simple<ValueTypes...> create_simple{};

using _create::void_cast;
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
