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

#include "kbdhook/sender_range.hpp"

#include <windows.h>
#include <windowsx.h>
#include <winuser.h>

#include <atomic>

template <typename Fn>
struct _keyboard_hook {
  Fn& fn_;
  unifex::inplace_stop_token token_;
  HHOOK hHook_;
  std::thread msgThread_;

  static inline std::atomic<_keyboard_hook*> self_{nullptr};

  explicit _keyboard_hook(Fn& fn, unifex::inplace_stop_token token)
    : fn_(fn)
    , token_(token)
    , hHook_(NULL)
    , msgThread_(
          [](_keyboard_hook* self) noexcept {
            _keyboard_hook* empty = nullptr;
            if (!self_.compare_exchange_strong(empty, self)) {
              std::terminate();
            }

            self->hHook_ =
                SetWindowsHookExW(WH_KEYBOARD_LL, &KbdHookProc, NULL, NULL);
            if (!self->hHook_) {
              LPCWSTR message = nullptr;
              FormatMessageW(
                  FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS,
                  NULL,
                  GetLastError(),
                  0,
                  (LPWSTR)&message,
                  128,
                  nullptr);

              printf("failed to set keyboard hook\n");
              printf("Error: %S\n", message);
              LocalFree((HLOCAL)message);
              std::terminate();
            }
            printf("keyboard hook set\n");

            MSG msg{};
            while (GetMessageW(&msg, NULL, 0, 0) &&
                   !self->token_.stop_requested()) {
              DispatchMessageW(&msg);
            }

            bool result =
                UnhookWindowsHookEx(std::exchange(self->hHook_, (HHOOK)NULL));
            if (!result) {
              std::terminate();
            }

            _keyboard_hook* expired = self;
            if (!self_.compare_exchange_strong(expired, nullptr)) {
              std::terminate();
            }

            printf("keyboard hook removed\n");
          },
          this) {}

  void join() {
    if (msgThread_.joinable()) {
      PostThreadMessageW(
          GetThreadId(msgThread_.native_handle()), WM_QUIT, 0, 0L);
      try {
        // there is a race in the windows thread implementation :(
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (msgThread_.joinable()) {
          msgThread_.join();
        }
      } catch (...) {
      }
    }
  }

  static LRESULT CALLBACK
  KbdHookProc(_In_ int nCode, _In_ WPARAM wParam, _In_ LPARAM lParam) {
    _keyboard_hook* self = self_.load();
    if (!!self && nCode >= 0 &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
      self->fn_(wParam);
      return CallNextHookEx(self->hHook_, nCode, wParam, lParam);
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
  }
};

namespace detail {
// create a range of senders where each sender completes on the next
// keyboard press
auto keyboard_events(unifex::inplace_stop_token token) {
  static auto register_ = [token](auto& fn) noexcept {
    return _keyboard_hook<decltype(fn)>{fn, token};
  };
  static auto unregister_ = [](auto& r) noexcept {
    r.join();
  };
  return std::make_pair(register_, unregister_);
}
}  // namespace detail
class keyboard_hook {
  using fns = decltype(detail::keyboard_events(
      std::declval<unifex::inplace_stop_token&>()));
  using RangeType = sender_range<
      WPARAM,
      unifex::inplace_stop_token,
      typename fns::first_type,
      typename fns::second_type>;

  RangeType range_;

public:
  explicit keyboard_hook(unifex::inplace_stop_token token)
    : range_(
          token,
          detail::keyboard_events(token).first,
          detail::keyboard_events(token).second) {}

  auto events() { return range_.view(); }
};
