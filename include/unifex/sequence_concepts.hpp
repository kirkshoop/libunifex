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
#include <unifex/type_traits.hpp>
#include <unifex/sender_concepts.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

namespace _sequence_connect {
  namespace _cpo {
    struct _fn;
  }
  template <typename Sender, typename Receiver, typename SenderFactory>
  using _member_sequence_connect_result_t =
      decltype((UNIFEX_DECLVAL(Sender&&)).sequence_connect(
          UNIFEX_DECLVAL(Receiver&&), UNIFEX_DECLVAL(SenderFactory&&)));

  template <typename Sender, typename Receiver, typename SenderFactory>
  UNIFEX_CONCEPT_FRAGMENT( //
    _has_member_sequence_connect_,  //
      requires() (         //
        typename(_member_sequence_connect_result_t<Sender, Receiver, SenderFactory>)
      ));
  template <typename Sender, typename Receiver, typename SenderFactory>
  UNIFEX_CONCEPT //
    _with_member_sequence_connect = //
      sender<Sender> &&
      UNIFEX_FRAGMENT(_sequence_connect::_has_member_sequence_connect_, Sender, Receiver, SenderFactory);

  template <typename Sender, typename Receiver, typename SenderFactory>
  UNIFEX_CONCEPT //
    _with_tag_invoke = //
      sender<Sender> && tag_invocable<_cpo::_fn, Sender, Receiver, SenderFactory>;

  namespace _cpo {
    struct _fn {
     private:
      template <typename Sender, typename Receiver, typename SenderFactory>
      static auto _select() {
        if constexpr (_with_tag_invoke<Sender, Receiver, SenderFactory>) {
          return meta_tag_invoke_result<_fn>{};
        } else if constexpr (_with_member_sequence_connect<Sender, Receiver, SenderFactory>) {
          return meta_quote3<_member_sequence_connect_result_t>{};
        } else {
          return type_always<void>{};
        }
      }
      template <typename Sender, typename Receiver, typename SenderFactory>
      using _result_t = typename decltype(_fn::_select<Sender, Receiver, SenderFactory>())
          ::template apply<Sender, Receiver, SenderFactory>;

     public:
      template(typename Sender, typename Receiver, typename SenderFactory)
        (requires receiver<Receiver> AND
            _with_tag_invoke<Sender, Receiver, SenderFactory>)
      auto operator()(Sender&& s, Receiver&& r, SenderFactory&& sf) const
          noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Receiver, SenderFactory>) ->
          _result_t<Sender, Receiver, SenderFactory> {
        return unifex::tag_invoke(_fn{}, (Sender &&) s, (Receiver &&) r, (SenderFactory&&)sf);
      }
      template(typename Sender, typename Receiver, typename SenderFactory)
        (requires receiver<Receiver> AND
            (!_with_tag_invoke<Sender, Receiver, SenderFactory>) AND
            _with_member_sequence_connect<Sender, Receiver, SenderFactory>)
      auto operator()(Sender&& s, Receiver&& r, SenderFactory&& sf) const
          noexcept(noexcept(((Sender &&) s).sequence_connect((Receiver &&) r, (SenderFactory&&) sf))) ->
          _result_t<Sender, Receiver, SenderFactory> {
        return ((Sender &&) s).sequence_connect((Receiver &&) r, (SenderFactory&&) sf);
      }
    };
  } // namespace _cpo
} // namespace _sequence_connect
inline const _sequence_connect::_cpo::_fn sequence_connect {};

#if UNIFEX_CXX_CONCEPTS
// Define the sequence_sender_to concept without macros for
// improved diagnostics:
template <typename Sender, typename Receiver, typename SenderFactory>
concept //
  sequence_sender_to = //
    sender<Sender> &&
    receiver<Receiver> &&
    requires (Sender&& s, Receiver&& r, SenderFactory&& sf) {
      sequence_connect((Sender&&) s, (Receiver&&) r, (SenderFactory&&) sf);
    };
#else
template <typename Sender, typename Receiver, typename SenderFactory>
UNIFEX_CONCEPT_FRAGMENT( //
  _sequence_sender_to, //
    requires (Sender&& s, Receiver&& r, SenderFactory&& sf) ( //
      sequence_connect((Sender&&) s, (Receiver&&) r, (SenderFactory&&) sf)
    ));
template <typename Sender, typename Receiver, typename SenderFactory>
UNIFEX_CONCEPT //
  sequence_sender_to =
    sender<Sender> &&
    receiver<Receiver> &&
    UNIFEX_FRAGMENT(_sequence_sender_to, Sender, Receiver, SenderFactory);
#endif

template <typename Sender, typename Receiver, typename SenderFactory>
using sequence_connect_result_t =
  decltype(sequence_connect(UNIFEX_DECLVAL(Sender), UNIFEX_DECLVAL(Receiver), UNIFEX_DECLVAL(SenderFactory)));

} // namespace unifex

#include <unifex/detail/epilogue.hpp>
