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
#include <unifex/combine_each.hpp>
#include <unifex/filter_each.hpp>
#include <unifex/interval.hpp>
#include <unifex/reduce_each.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sequence_concepts.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/then_each.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/with_query_value.hpp>

using namespace unifex;

using namespace std::literals::chrono_literals;

const auto gap = std::array{3ms, 5ms};
const auto initial = 200ms;
const auto sample = 100;
[[maybe_unused]] const auto expected = sample * 1ms;

int main() {
  auto time = std::array{
      unifex::timed_single_thread_context{},
      unifex::timed_single_thread_context{}};

  {
    printf("in 200ms have two threads tick every 3ms and 5ms respectively and "
           "stop after 12 samples\n");
    printf("sample every ~100ms, report the delta between the actual and "
           "expected time\n");

    auto start = std::chrono::steady_clock::now();
    auto first = start + initial;

    auto tickCount = unifex::sync_wait(
        // send intervals (first, first + gap, etc..)
        unifex::combine_each(
            unifex::interval(first, gap[0]),
            unifex::interval(first, gap[1]) |
                // use a different thread for this interval
                with_query_value(
                    unifex::get_scheduler, time[1].get_scheduler())) |  //
        // only sample 1 tick out of the expected, ignore the rest
        filter_each(
            [&](auto tick) { return (tick - first) % expected < gap[0]; }) |  //
        // include actual time of event
        then_each([](auto tick) {
          return std::make_tuple(std::chrono::steady_clock::now(), tick);
        }) |  //
        // log ticks that arrive and accumulate a count of the ticks that
        // arrived
        reduce_each(
            0,
            [&](int count, const auto& itemSender) {
              return itemSender |  //
                  unifex::then([&, count]([[maybe_unused]] auto tpl) {
                       auto& [actual, intended] = tpl;
                       auto delta =
                           std::chrono::duration_cast<
                               std::chrono::duration<float, std::milli>>(
                               actual - intended)
                               .count();
                       auto millis =
                           std::chrono::duration_cast<
                               std::chrono::duration<float, std::milli>>(
                               actual - first)
                               .count();
                       printf(
                           "delta is %.4fms at sample %3d, %3.4fms after "
                           "initial tick\n",
                           delta,
                           count,
                           millis);
                       return count + 1;
                     });
            }) |  //
        // cancel the infinite interval sequence after 6 samples
        unifex::stop_when(
            unifex::schedule_at(first + (expected * 6)) |  //
            then([] { printf("stop\n"); })) |
        // use this scheduler implicitly (first interval and the stop_when
        // trigger)
        with_query_value(unifex::get_scheduler, time[0].get_scheduler()));
    auto millis =
        std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(
            std::chrono::steady_clock::now() - start)
            .count();
    printf("emitted %d samples in %3.4fms\n", *tickCount, millis);
  }
}