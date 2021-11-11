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

const auto gap = 1ms;
const auto initial = 200ms;
const auto sample = 100;
[[maybe_unused]] const auto expected = gap * sample;

int main() {
  unifex::timed_single_thread_context time;

  {
    printf("in 200ms tick every 1ms and stop after 6 samples\n");
    printf("sample every 100 ticks, report the delta between the actual and "
           "expected time\n");

    auto start = std::chrono::steady_clock::now();
    auto first = start + initial;

    auto tickCount = unifex::sync_wait(
        // send intervals (first, first + gap, etc..)
        unifex::interval(first, gap) |  //
        // only sample 1 tick out of the expected, ignore the rest
        filter_each(
            [&](auto tick) { return (tick - first) % expected == 0ms; }) |  //
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
            then([] { printf("stop\n"); })) |              //
        with_query_value(unifex::get_scheduler, time.get_scheduler()));
    auto millis =
        std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(
            std::chrono::steady_clock::now() - start)
            .count();
    printf("emitted %d samples in %3.4fms\n", *tickCount, millis);
  }
}