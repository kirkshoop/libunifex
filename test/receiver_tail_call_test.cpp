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
#include <unifex/receiver_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/with_query_value.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/just.hpp>
#include <unifex/then.hpp>
#include <unifex/repeat_effect_until.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

TEST(ReceiverTailCall, Smoke) {
  unifex::timed_single_thread_context time;
  int iterations = 0;
  sync_wait(
    just() 
    | then([&iterations]{++iterations;})
    | repeat_effect()
    | unifex::stop_when(
      unifex::schedule_after(2s))
    | with_query_value(unifex::get_scheduler, time.get_scheduler())
  );

  std::cout << "result: there were " << iterations << " iterations in 2s\n";
}