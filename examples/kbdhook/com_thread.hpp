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

#include <unifex/manual_event_loop.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/timed_single_thread_context.hpp>

#include <chrono>
#include <thread>

#include <windows.h>

template <typename Sched>
using scheduler_for_t =
    decltype(std::declval<Sched&>().get_scheduler());

struct com_thread {
  using run_scheduler_t = scheduler_for_t<unifex::manual_event_loop>;
  using time_scheduler_t = scheduler_for_t<unifex::timed_single_thread_context>;
  using duration_t = unifex::timed_single_thread_context::clock_t::duration;

  duration_t maxTime_;
  unifex::timed_single_thread_context time_;
  unifex::manual_event_loop run_;
  std::thread comThread_;

  static void event_pump_(com_thread* self) noexcept {
    {  // create message queue
      MSG msg;
      PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    }
    printf("com thread start\n");
    fflush(stdout);

    if (FAILED(CoInitializeEx(
            nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
      std::terminate();
    }

    unifex::scope_guard exit{[self]() noexcept {
      self->run_.stop();
      self->run_.run();  // run until empty

      CoUninitialize();

      printf("com thread exit\n");
      fflush(stdout);
    }};

    BOOL pendingMessages = FALSE;
    MSG msg = {};
    while ((pendingMessages = GetMessage(&msg, NULL, 0, 0)) != 0) {
      if (pendingMessages == -1) {
        std::terminate();
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      unifex::sync_wait(
          self->run_.run_as_sender() |
          unifex::stop_when(unifex::schedule_after(
              self->time_.get_scheduler(), self->maxTime_)));
    }
  }

  explicit com_thread(duration_t maxTime)
    : maxTime_(maxTime)
    , comThread_(event_pump_, this) {}
  ~com_thread() { join(); }

  template <typename Receiver>
  struct state {
    using sender_t = unifex::schedule_result_t<run_scheduler_t>;
    using op_t = unifex::connect_result_t<sender_t, Receiver&>;

    com_thread* self_;
    op_t op_;

    state(com_thread* self, sender_t sender, Receiver rec)
      : self_(self)
      , op_(unifex::connect(std::move(sender), rec)) {
      unifex::start(op_);
      // wake up the message loop
      while (self_->comThread_.joinable() &&
              !PostThreadMessageW(
                  GetThreadId(self_->comThread_.native_handle()),
                  WM_USER,
                  0,
                  0L)) {
      }
    }
  };

  struct _scheduler {
    com_thread* self_;

    auto schedule() {
      return unifex::create_simple<>([self = self_]<class R>(R& rec) {
        return state<R>{self, unifex::schedule(self->run_.get_scheduler()), rec};
      });
    }

    bool operator==(_scheduler const &a) const noexcept {
      return a.self_->run_.get_scheduler() == self_->run_.get_scheduler();
    }
  };
  _scheduler get_scheduler() { return _scheduler{this}; }

  void join() {
    if (comThread_.joinable()) {
      if (!PostThreadMessageW(
              GetThreadId(comThread_.native_handle()), WM_QUIT, 0, 0L)) {
        std::terminate();
      }
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