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

#include <unifex/tag_invoke.hpp>
#include <unifex/type_traits.hpp>
#include <unifex/detail/unifex_fwd.hpp>

#include <tuple>
#include <type_traits>

#include <unifex/detail/prologue.hpp>

namespace unifex {

template <typename T>
UNIFEX_CONCEPT_FRAGMENT(    //
    _callable_package_exp,  //
    requires(T t)           //
    (static_cast<typename T::value_type>(t),
     static_cast<typename T::value_type>(std::move(t)),
     t(),
     std::move(t)()));

template <typename T>
UNIFEX_CONCEPT callable_package =                //
    (std::is_nothrow_move_constructible_v<T>)&&  //
    (std::is_nothrow_move_assignable_v<T>)&&     //
    UNIFEX_FRAGMENT(_callable_package_exp, T);   //

template <typename CPO, typename Target, typename... As>
struct packaged_callable {
  using value_type = unifex::callable_result_t<CPO, Target, As...>;
  CPO cpo_;
  Target t_;
  std::tuple<As...> as_;
  packaged_callable() = delete;
  explicit packaged_callable(CPO cpo, Target t, As... as)        //
      noexcept((std::is_nothrow_move_constructible_v<CPO>)&&     //
               (std::is_nothrow_move_constructible_v<Target>)&&  //
               (std::is_nothrow_move_constructible_v<As>&&...))
    : cpo_(std::move(cpo))
    , t_(std::move(t))
    , as_(std::move(as)...) {}
  explicit operator value_type() &  //
      noexcept(is_nothrow_callable_v<CPO, Target, As...>) {
    return std::apply([&](auto... as) { return cpo_(t_, as...); }, as_);
  }
  value_type operator()() &  //
      noexcept(is_nothrow_callable_v<CPO, Target, As...>) {
    return std::apply([&](auto... as) { return cpo_(t_, as...); }, as_);
  }

  explicit operator value_type() const&  //
      noexcept(is_nothrow_callable_v<CPO, Target, As...>) {
    return std::apply([&](auto... as) { return cpo_(t_, as...); }, as_);
  }
  value_type operator()() const&  //
      noexcept(is_nothrow_callable_v<CPO, Target, As...>) {
    return std::apply([&](auto... as) { return cpo_(t_, as...); }, as_);
  }

  explicit operator value_type() &&  //
      noexcept(is_nothrow_callable_v<CPO, Target&&, As&&...>) {
    return std::apply(
        [cpo = std::move(cpo_), t = std::move(t_)](auto&&... as) mutable {
          return cpo(std::move(t), std::move(as)...);
        },
        std::move(as_));
  }
  value_type operator()() &&  //
      noexcept(is_nothrow_callable_v<CPO, Target&&, As&&...>) {
    return std::apply(
        [cpo = std::move(cpo_), t = std::move(t_)](auto&&... as) mutable {
          return cpo(std::move(t), std::move(as)...);
        },
        std::move(as_));
  }
};

template(typename CPO, typename Target, typename... As)  //
    (requires                                            //
     (std::is_nothrow_move_constructible_v<CPO>) &&      //
     (std::is_nothrow_move_constructible_v<Target>)&&    //
     (std::is_nothrow_move_constructible_v<As> && ...))  //
    packaged_callable(CPO, Target, As...)
        ->packaged_callable<CPO, Target, As...>;

template <typename CPO, typename Target, typename... As>  //
using packaged_callable_t = packaged_callable<CPO, Target, As...>;

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
