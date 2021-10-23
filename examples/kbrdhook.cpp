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

#include <unifex/done_as_optional.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sequence.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>

#include <cassert>
#include <chrono>
#include <ranges>

#include "kbdhook/clean_stop.hpp"
#include "kbdhook/com_thread.hpp"
#include "kbdhook/keyboard_hook.hpp"
#include "kbdhook/player.hpp"

unifex::task<void> clickety(Player& player, keyboard_hook& keyboard) {
  auto keystrikes =
      keyboard.events() |
      std::views::transform(unifex::done_as_optional);

  for (auto next : keystrikes) {
    auto evt = co_await next;
    if (!evt) {
      break;
    }
    player.Click();
  }

  co_return;
}

int wmain() {
  printf("main start\n");
  unifex::scope_guard mainExit{[]() noexcept {
    printf("main exit\n");
  }};

  using namespace std::literals::chrono_literals;

  com_thread com{50ms};
  clean_stop exit;
  Player player{com.get_scheduler()};
  keyboard_hook keyboard{com.get_scheduler()};

  unifex::sync_wait(
    unifex::sequence(
      // start
      player.start(),
      keyboard.start(),
      unifex::just_from([]() { printf("press ctrl-C to stop...\n"); }),
      // click
      clickety(player, keyboard) |
          unifex::stop_when(
              // until ctrl+C
              exit.event()),
        // stop
        keyboard.destroy(),
        player.destroy()));
}
