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

#include <unifex/sender_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/manual_event_loop.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/stop_when.hpp>

#include <chrono>

#include <windows.h>
#include <windowsx.h>
#include <winuser.h>

struct com_thread {
  unifex::manual_event_loop& run_;
  using time_scheduler_t = decltype(std::declval<unifex::timed_single_thread_context&>().get_scheduler());
  time_scheduler_t time_;
  using duration_t = typename unifex::timed_single_thread_context::clock_t::duration;
  duration_t maxTime_;
  std::thread comThread_;
  explicit com_thread(
      unifex::manual_event_loop& run,
      time_scheduler_t time,
      duration_t maxTime)
    : run_(run)
    , time_(time)
    , maxTime_(maxTime)
    , comThread_([this] {
      printf("com thread start\n");

      if (FAILED(CoInitializeEx(
              nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        std::terminate();
      }

      MSG msg{};
      while (GetMessageW(&msg, NULL, 0, 0)) {
        //unifex::sync_wait(
        //    run_.run_as_sender() |
        //    unifex::stop_when(unifex::schedule_after(time_, maxTime_)));
        DispatchMessageW(&msg);
      }

      CoUninitialize();

      printf("com thread exit\n");
    }) {}

  void join() {
    if (comThread_.joinable()) {
      PostThreadMessageW(
          GetThreadId(comThread_.native_handle()), WM_QUIT, 0, 0L);
      try {
        // there is a race in the windows thread implementation :(
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (comThread_.joinable()) {
          comThread_.join();
        }
      } catch (...) {
      }
    }
  }
};