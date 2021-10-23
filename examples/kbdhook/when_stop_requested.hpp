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

#include <unifex/create.hpp>
#include <unifex/bind_back.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/receiver_concepts.hpp>

#include <concepts>

template <typename Predecessor, typename Fn>
struct when_stop_requested_sender {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = unifex::sender_value_types_t<Predecessor, Variant, Tuple>;

  template <template <typename...> class Variant>
  using error_types = unifex::sender_error_types_t<Predecessor, Variant>;

  static constexpr bool sends_done =
      unifex::sender_traits<unifex::remove_cvref_t<Predecessor>>::sends_done;

  template <typename OpState, typename Callback>
  struct operation {
    void start() noexcept {
      unifex::start(state_);
    }
    OpState state_;
    Callback callback_;
  };

  template <typename Receiver>
  using callback_t =
    typename unifex::stop_token_type_t<Receiver>::
      template callback_type<Fn>;

  template <typename Sender, typename Receiver>
  using operation_t = operation<
      unifex::connect_result_t<Sender, Receiver>,
      callback_t<Receiver>>;

  template <typename Receiver>
    requires unifex::sender_to<Predecessor, Receiver>  //
  operation_t<Predecessor, Receiver> connect(Receiver&& rec) && {
    auto token = unifex::get_stop_token(rec);
    return {
        unifex::connect(std::move(pred_), (Receiver &&) rec),
        callback_t<Receiver>{token, std::move(fn_)}};
  }

  template <typename Receiver>
    requires unifex::sender_to<Predecessor const&, Receiver>  //
  operation_t<Predecessor const&, Receiver> connect(Receiver && rec) const& {
    auto token = unifex::get_stop_token(rec);
      return {
          unifex::connect(pred_, (Receiver &&) rec),
          callback_t<Receiver>{token, fn_}};
  }

  Predecessor pred_;
  Fn fn_;
};

inline constexpr struct _when_stop_requested {
  template <typename Sender, typename Fn>
    requires std::invocable<Fn>
  auto operator()(Sender&& sender, Fn fn) const -> //
    when_stop_requested_sender<std::decay_t<Sender>, Fn> {
    return {(Sender &&) sender, std::move(fn)};
  }

  template <typename Fn>
    requires std::invocable<Fn>
  auto operator()(Fn fn) const {
    return unifex::bind_back(*this, std::move(fn));
  }
} when_stop_requested{};
