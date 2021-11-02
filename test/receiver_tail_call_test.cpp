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
#include <unifex/just.hpp>
#include <unifex/let_done.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/when_all.hpp>
#include <unifex/with_query_value.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

auto loopDuration = 2s;

TEST(ReceiverTailCall, Smoke) {
  unifex::timed_single_thread_context time;
  std::int64_t iterations = 0;
  sync_wait(
      just()                                                     //
      | then([&iterations] { ++iterations; })                    //
      | repeat_effect()                                          //
      | unifex::stop_when(unifex::schedule_after(loopDuration))  //
      | let_done([] { return just(); })                          //
      |
      then([&iterations] {
        std::cout << "result: there were " << iterations << " iterations in "
                  << loopDuration.count() << "s which is "
                  << (double(
                          std::chrono::duration_cast<std::chrono::nanoseconds>(
                              loopDuration)
                              .count()) /
                      iterations)
                  << " ns-per-iteration\n";
      })  //
      | with_query_value(unifex::get_scheduler, time.get_scheduler()));
}

TEST(ReceiverTailCall, ForLoop) {
  unifex::timed_single_thread_context time;
  std::int64_t iterations = 0;
  std::atomic_flag stop_requested{false};
  auto op = connect(
      schedule_after(time.get_scheduler(), loopDuration) |
          then([&stop_requested] { stop_requested.test_and_set(); }),
      null_tail_receiver{});
  start(op);
  for (; !stop_requested.test();) {
    ++iterations;
  }
  std::cout << "result: there were " << iterations << " iterations in "
            << loopDuration.count() << "s which is "
            << (double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                           loopDuration)
                           .count()) /
                iterations)
            << " ns-per-iteration\n";
}

TEST(ReceiverTailCall, ScheduleEachRepeat) {
  unifex::timed_single_thread_context time;
  std::int64_t iterations = 0;
  sync_wait(
      schedule()                               //
      | then([&iterations] { ++iterations; })  //
      | repeat_effect()                        //
      | unifex::stop_when(
            unifex::schedule_after(time.get_scheduler(), loopDuration))  //
      | let_done([] { return just(); })                                  //
      |
      then([&iterations] {
        std::cout << "result: there were " << iterations << " iterations in "
                  << loopDuration.count() << "s which is "
                  << (double(
                          std::chrono::duration_cast<std::chrono::nanoseconds>(
                              loopDuration)
                              .count()) /
                      iterations)
                  << " ns-per-iteration\n";
      }));
}

TEST(ReceiverTailCall, InterleaveScheduleEachRepeatLoops) {
  unifex::timed_single_thread_context time;
  std::int64_t iterations = 0;
  auto loop = [&]() {
    return schedule()                            //
        | then([&iterations] { ++iterations; })  //
        | repeat_effect();
  };
  sync_wait(
      when_all(loop(), loop()) |
      unifex::stop_when(
          unifex::schedule_after(time.get_scheduler(), loopDuration))  //
      | let_done([] { return just(); })                                //
      |
      then([&iterations](auto&&...) {
        std::cout << "result: there were " << iterations << " iterations in "
                  << loopDuration.count() << "s which is "
                  << (double(
                          std::chrono::duration_cast<std::chrono::nanoseconds>(
                              loopDuration)
                              .count()) /
                      iterations)
                  << " ns-per-iteration\n";
      }));
}
