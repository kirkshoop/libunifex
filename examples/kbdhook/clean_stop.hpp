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

#include <unifex/inplace_stop_token.hpp>

#include <windows.h>
#include <windowsx.h>
#include <winuser.h>

#include <atomic>

struct clean_stop {
  static inline std::atomic<unifex::inplace_stop_source*> stop_{nullptr};

  ~clean_stop() {
    if (!SetConsoleCtrlHandler(&consoleHandler, FALSE)) {
      std::terminate();
    }
    if (stop_.exchange(nullptr) == nullptr) {
      std::terminate();
    }
  }
  explicit clean_stop(unifex::inplace_stop_source& stop) {
    if (stop_.exchange(&stop) != nullptr) {
      std::terminate();
    }
    if (!SetConsoleCtrlHandler(&consoleHandler, TRUE)) {
      std::terminate();
    }
  }
  static BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
      printf("\n");  // end the line of '.'
      stop_.load()->request_stop();
    }
    return TRUE;
  }
};
