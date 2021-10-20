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

#include <unifex/sender_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/with_query_value.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/coroutine.hpp>
#include <unifex/task.hpp>

#include <cassert>

#include "kbdhook/sender_range.hpp"
#include "kbdhook/keyboard_hook.hpp"
#include "kbdhook/player.hpp"
#include "kbdhook/clean_stop.hpp"

// create a range of senders where each sender completes on the next 
// keyboard press
auto keyboard_events(unifex::inplace_stop_token token) {
  return create_event_sender_range<WPARAM>(
      token,
      [&](auto& fn) noexcept {
        return keyboard_hook{fn, token};
      },
      [](auto& r) noexcept { r.join(); });
}

auto with_stop_token(unifex::inplace_stop_token token) {
  return unifex::with_query_value(unifex::get_stop_token, token);
}

//#pragma optimize("", off )
unifex::task<void> clickety(Player& player, unifex::inplace_stop_token token) {

  for (auto next : keyboard_events(token)) {
    auto evt =
        co_await (next | with_stop_token(token));
    if (!evt) {
      break;
    }
    player.Click();
  }

  co_return;
}

int wmain() {
  unifex::inplace_stop_source stopSource;
  clean_stop exit{stopSource};
  Player player;
  unifex::sync_wait(clickety(player, stopSource.get_token()));
  player.join();
  printf("main exit\n");
}