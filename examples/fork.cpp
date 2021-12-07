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
#include <unifex/async_scope.hpp>
#include <unifex/bounded_storage.hpp>
#include <unifex/filter_each.hpp>
#include <unifex/fork.hpp>
#include <unifex/let_done.hpp>
#include <unifex/reduce_each.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sequence_concepts.hpp>
#include <unifex/static_thread_pool.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/then_each.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/with_query_value.hpp>

#include <exception>

using namespace unifex;

#include <iostream>

using namespace std::literals::chrono_literals;

struct atomic_counter {
  std::atomic<int>* count_;
  int local_ = 0;
  friend atomic_counter operator+(atomic_counter c, int i) {
    c.local_ = c.count_->fetch_add(i);
    return c;
  }
  operator int() { return local_; }
  int load() { return count_->load(); }
};

template <typename Scheduler>
auto with_scheduler(Scheduler s) {
  return unifex::with_query_value(unifex::get_scheduler, s);
}

template <typename Storage>
auto with_storage(Storage s) {
  return unifex::with_query_value(unifex::get_storage, s);
}

int main() {
  timed_single_thread_context time;
  auto tm = time.get_scheduler();
  static_thread_pool pool{4};
  auto tp = pool.get_scheduler();
  bounded_storage<4> st;
  async_scope scp;

  {
    auto start = std::chrono::steady_clock::now();
    std::atomic<int> acount{0};

    auto count = unifex::sync_wait(
        // create items on a thread pool using st to bound the number of
        // concurrent items in flight
        unifex::fork() | with_scheduler(tp) | with_storage(st) |
        // record the thread id for each item
        unifex::then_each([&]() {
          // this is multi-threaded because of static_thread_pool
          return std::make_tuple(
              std::this_thread::get_id(), std::chrono::steady_clock::now());
        }) |
        // suppress the set_done that results from stop_when so that
        // reduce produces the count
        // too clever by half: inject a wait for all the spawned ops to complete
        unifex::let_done([&] { return scp.complete(); }) |
        // log items that arrive and accumulate a count of the samples that
        // arrived
        unifex::reduce_each(
            atomic_counter{&acount},
            [&](const auto& itemSender) {
              return itemSender |  //
                  then([&](atomic_counter count, auto tpl) {
                       auto& [id, at] = tpl;
                       auto thisCount = count + 1;
                       // need to spawn output on a strand since cout
                       // and its ilk are not thread-safe on some platforms
                       scp.spawn_call_on(
                           tm,
                           [id = id,
                            at = at,
                            count = (int)thisCount,
                            start]() noexcept {
                             auto nowmicros =
                                 std::chrono::duration_cast<
                                     std::chrono::duration<float, std::micro>>(
                                     at - start)
                                     .count();
                             std::cout << "[" << id << "] item " << count
                                       << " at " << nowmicros << "us\n";
                             fflush(stdout);
                           });
                       return thisCount;
                     });
            }) |
        // cancel the infinite fork()
        unifex::stop_when(
            unifex::schedule_after(1ms) |      //
            then([] { printf("stop\n"); })) |  //
        with_scheduler(tm));
    auto millis =
        std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(
            std::chrono::steady_clock::now() - start)
            .count();
    printf("emitted %d samples in %3.4fms\n", count->load(), millis);
  }
}
