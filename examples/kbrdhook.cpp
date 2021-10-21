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

#include <unifex/coroutine.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>
#include <unifex/with_query_value.hpp>

#include <cassert>

#include "kbdhook/clean_stop.hpp"
#include "kbdhook/keyboard_hook.hpp"
#include "kbdhook/player.hpp"

auto with_stop_token(unifex::inplace_stop_token token) {
  return unifex::with_query_value(unifex::get_stop_token, token);
}

unifex::task<void> clickety(Player& player, unifex::inplace_stop_token token) {
  keyboard_hook keyboard{token};

  for (auto next : keyboard.events()) {
    auto evt = co_await(next | with_stop_token(token));
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