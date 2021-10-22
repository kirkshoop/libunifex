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

#include <unifex/create.hpp>

#include <windows.h>

#include <atomic>

struct clean_stop {
  static inline std::atomic<void*> receiver_{nullptr};
  static inline std::atomic<void(*)(void*)> set_value_fn_{nullptr};

  clean_stop() {
    if (!SetConsoleCtrlHandler(&consoleHandler, TRUE)) {
      std::terminate();
    }
  }
  ~clean_stop() {
    if (!SetConsoleCtrlHandler(&consoleHandler, FALSE)) {
      std::terminate();
    }
  }
  [[nodiscard]] auto event() {
    return unifex::create_simple<>([]<class R>(R& rec) {
      auto set_value_fn = [](void* p) {
        unifex::set_value(std::move(*static_cast<R*>(p)));
      };
      if (nullptr != receiver_.exchange(&rec) ||
          nullptr != set_value_fn_.exchange(set_value_fn)) {
        std::terminate();
      }
    });
  }
  static BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
      set_value_fn_.load()(receiver_.load());
    }
    return TRUE;
  }
};
