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

#include <unifex/detail/atomic_intrusive_queue.hpp>

#include <unifex/sender_concepts.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/create.hpp>
#include <unifex/just.hpp>
#include <unifex/then.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/let_value.hpp>
#include <unifex/with_query_value.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/range.hpp>

#include <chrono>
#include <optional>

#include <comip.h>

using namespace std::literals::chrono_literals;

namespace detail {
// _conv needed so we can emplace construct non-movable types into
// a std::optional.
template <typename F>
struct _conv {
  F f_;
  explicit _conv(F f) noexcept : f_((F &&) f) {}
  operator std::invoke_result_t<F>() && { return ((F &&) f_)(); }
};
}  // namespace detail

template <
    typename EventType,
    typename RangeStopToken,
    typename RegisterFn,
    typename UnregisterFn>
struct sender_range {
  using complete_function_t = void (*)(void*, EventType*) noexcept;

  struct pending_operation {
    void* pendingOperation_;
    complete_function_t complete_with_event_;

    void operator()(EventType* e) {
      std::exchange(complete_with_event_, nullptr)(
          std::exchange(pendingOperation_, nullptr), e);
    };

    pending_operation* next_{nullptr};
  };

  template <typename State>
  void start(State* state) noexcept {
    if (rangeToken_.stop_requested() ||
        state->eventStopToken_.stop_requested()) {
      unifex::set_done(std::move(state->rec_));
    } else {
      (void)pendingOperations_.enqueue(&state->pending_);
    }
  }

  void dispatch(EventType* event) {
    auto pending = pendingOperations_.dequeue_all();

    if (pending.empty()) {
      // no pending operations to complete, discard this event
      return;
    }

    auto& complete = *pending.pop_front();
    if (!pending.empty()) {
      // more than one pending operation - bug in sender_range usage (do not
      // start a sender from the range until the previous sender has completed.)
      std::terminate();
    }

    complete(event);
  }

  void stop_pending() { dispatch(nullptr); }

  struct event_function {
    sender_range* scope_;
    explicit event_function(sender_range* scope) : scope_(scope) {}
    template <typename EventType2>
    void operator()(EventType2&& event) {
      scope_->dispatch(&event);
    }
  };

  using registration_t =
    unifex::callable_result_t<RegisterFn, event_function&>;

  struct create_sender {
    template<template<typename...> class Variant, template<typename...> class Tuple>
    using value_types = Variant<Tuple<EventType>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static inline constexpr bool sends_done = true;

    template <typename Receiver, typename EventStopToken>
    struct state {
      static void
      _complete_with_event(void* selfVoid, EventType* event) noexcept {
        auto& self = *reinterpret_cast<state*>(selfVoid);
        if (!!event) {
          unifex::set_value(std::move(self.rec_), std::move(*event));
        } else {
          unifex::set_done(std::move(self.rec_));
        }
      }

      // args
      sender_range* scope_;
      Receiver& rec_;
      EventStopToken eventStopToken_;

      // this is stored in an intrusive queue so that the event_function can dequeue and dispatch the next event
      pending_operation pending_;

      // cancellation of the pending sender
      struct stop_callback {
        sender_range* scope_;
        void operator()() noexcept { scope_->stop_pending(); }
      };
      typename EventStopToken::template callback_type<stop_callback> callback_;

      state(sender_range* scope, Receiver& rec, EventStopToken eventStopToken)
        : scope_(scope)
        , rec_(rec)
        , eventStopToken_(eventStopToken) 
        , pending_({this, &_complete_with_event})
        , callback_(eventStopToken_, stop_callback{scope_}) {
        scope_->start(this);
      }
      state() = delete;
      state(const state&) = delete;
      state(state&&) = delete;
    };

    template<typename Receiver>
    state<Receiver, unifex::remove_cvref_t<unifex::callable_result_t<unifex::tag_t<unifex::get_stop_token>, Receiver>>> 
    operator()(Receiver& rec, sender_range* scope) noexcept {
      auto eventStopToken = unifex::get_stop_token(rec);
      return {scope, rec, eventStopToken};
    }
  };

  struct stop_callback {
    sender_range* scope_;
    void operator()() noexcept { scope_->_unregister(); }
  };

  // this function is used to provide a stable type for the expression inside (that uses a lambda)
  static auto make_range(sender_range* self) {
    return ::ranges::views::iota(0)
    | ::ranges::views::transform(
        [self](int) { return unifex::create(create_sender{}, self); });
  }

  // storage for underlying range that produces senders
  unifex::callable_result_t<decltype(&make_range), sender_range*> range_;
  // args
  RangeStopToken rangeToken_;
  RegisterFn registerFn_;
  UnregisterFn unregisterFn_;
  // cancellation
  typename RangeStopToken::template callback_type<stop_callback> callback_;
  // tracking result of registerFn
  std::optional<registration_t> registration_;
  // type-erased registration of a sender waiting for an event
  unifex::atomic_intrusive_queue<pending_operation, &pending_operation::next_>
      pendingOperations_;
  // fixed storage for the fucntion used to emit an event (allows event_function& to have the right lifetime)
  event_function event_function_;

  auto _register() noexcept {
    return detail::_conv{[this]() noexcept {
      return registerFn_(event_function_);
    }};
  }

  void _unregister() {
    if (!!registration_) {
      unregisterFn_(registration_.value());
      registration_.reset();
      stop_pending();
    }
  }

public:
  ~sender_range() noexcept { _unregister(); }
  explicit sender_range(RangeStopToken token, RegisterFn registerFn, UnregisterFn unregisterFn)
    : range_(make_range(this))
    , rangeToken_(token)
    , registerFn_(registerFn)
    , unregisterFn_(unregisterFn) 
    , callback_(rangeToken_, stop_callback{this}) 
    , registration_(_register())
    , pendingOperations_()
    , event_function_(this) {}
  sender_range() = delete;
  sender_range(const sender_range&) = delete;
  sender_range(sender_range&&) = delete;

  auto begin() noexcept { return range_.begin(); }
  auto end() noexcept { return range_.end(); }
};

template<typename EventType, typename StopToken, typename RegisterFn, typename UnregisterFn>
sender_range<EventType, StopToken, RegisterFn, UnregisterFn>
create_event_sender_range(StopToken token, RegisterFn&& registerFn, UnregisterFn&& unregisterFn) {
    using result_t = sender_range<EventType, StopToken, RegisterFn, UnregisterFn>;
    using registration_t =
        unifex::callable_result_t<RegisterFn, typename result_t::event_function&>;

    static_assert(unifex::is_nothrow_callable_v<RegisterFn, typename result_t::event_function&>, "register function must be noexcept");
    static_assert(unifex::is_nothrow_callable_v<UnregisterFn, registration_t&>, "unregister function must be noexcept");

    return result_t{token, (RegisterFn&&) registerFn, (UnregisterFn&&) unregisterFn};
}

#include <windows.h>
#include <windowsx.h>
#include <winuser.h>
#include <mfplay.h>
#pragma comment(lib, "mfplay.lib")
#include <mferror.h>
#include <shobjidl.h>   // defines IFileOpenDialog
#include <Shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#include <strsafe.h>

struct Player {
  class MediaPlayerCallback : public IMFPMediaPlayerCallback {
    Player* player_;
    long m_cRef;  // Reference count

  public:
    explicit MediaPlayerCallback(Player* player) : player_(player), m_cRef(1) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
      static const QITAB qit[] = {
          QITABENT(MediaPlayerCallback, IMFPMediaPlayerCallback),
          {0},
      };
      return QISearch(this, qit, riid, ppv);
    }
    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
    STDMETHODIMP_(ULONG) Release() {
      ULONG count = InterlockedDecrement(&m_cRef);
      if (count == 0) {
        delete this;
        return 0;
      }
      return count;
    }

    // IMFPMediaPlayerCallback methods
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader) {
      if (FAILED(pEventHeader->hrEvent)) {
        player_->ShowErrorMessage(L"Playback error", pEventHeader->hrEvent);
        return;
      }

      switch (pEventHeader->eEventType) {
        case MFP_EVENT_TYPE_MEDIAITEM_CREATED:
          player_->OnMediaItemCreated(
              MFP_GET_MEDIAITEM_CREATED_EVENT(pEventHeader));
          break;

        case MFP_EVENT_TYPE_MEDIAITEM_SET:
          player_->OnMediaItemSet(MFP_GET_MEDIAITEM_SET_EVENT(pEventHeader));
          break;
      }
    }
  };

  UINT WM_PLAYER_CLICK;
  UINT WM_PLAYER_SHOWERROR;
  UINT WM_PLAYER_ITEMCREATED;
  UINT WM_PLAYER_ITEMSET;
  std::thread comThread_;

  Player()
    : WM_PLAYER_CLICK(RegisterWindowMessageW(L"PlayerClick"))
    , WM_PLAYER_SHOWERROR(RegisterWindowMessageW(L"PlayerShowError"))
    , WM_PLAYER_ITEMCREATED(RegisterWindowMessageW(L"PlayerItemCreated"))
    , WM_PLAYER_ITEMSET(RegisterWindowMessageW(L"PlayerItemSet"))
    , comThread_([this] {
      if (FAILED(CoInitializeEx(
              NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        std::terminate();
      }

      HRESULT hr = S_OK;
      auto pPlayerCB = std::make_unique<MediaPlayerCallback>(
          this);                           // Application callback object.
      IMFPMediaPlayer* pPlayer = nullptr;  // The MFPlay player object.
      unifex::scope_guard releasePlayer{[&]() noexcept {
        if (!!pPlayer) {
          std::exchange(pPlayer, nullptr)->Release();
        }
      }};
      hr = MFPCreateMediaPlayer(
          NULL,
          FALSE,            // Start playback automatically?
          0,                // Flags
          pPlayerCB.get(),  // Callback pointer
          NULL,             // Video window
          &pPlayer);
      if (FAILED(hr)) {
        std::terminate();
      }

      // Create a new media item for this URL.
      hr = pPlayer->CreateMediaItemFromURL(
          L"https://webwit.nl/input/kbsim/mp3/1_.mp3", FALSE, 0, NULL);
      if (FAILED(hr)) {
        std::terminate();
      }

      MSG msg{};
      while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_PLAYER_CLICK) {
          HRESULT hr = pPlayer->Stop();
          if (FAILED(hr)) {
            std::terminate();
          }
          hr = pPlayer->Play();
          if (FAILED(hr)) {
            std::terminate();
          }

          printf(".");
          fflush(stdout);
          continue;
        } else if (msg.message == WM_PLAYER_SHOWERROR) {
          HRESULT hr = S_OK;
          WCHAR str[MAX_PATH];

          hr = StringCbPrintfW(
              str,
              sizeof(str),
              L"%s (hr=0x%X)",
              (PCWSTR)msg.lParam,
              (HRESULT)msg.wParam);

          if (SUCCEEDED(hr)) {
            MessageBoxW(NULL, str, L"Error", MB_ICONERROR);
          }
          continue;
        } else if (msg.message == WM_PLAYER_ITEMCREATED) {
          HRESULT hr = S_OK;
          IMFPMediaItem* pMediaItem = (IMFPMediaItem*)msg.lParam;
          unifex::scope_guard releaseItem{[&]() noexcept {
            if (!!pMediaItem) {
              std::exchange(pMediaItem, nullptr)->Release();
            }
          }};

          // The media item was created successfully.

          if (pPlayer) {
            // Set the media item on the player. This method completes
            // asynchronously.
            hr = pPlayer->SetMediaItem(pMediaItem);
          }

          if (FAILED(hr)) {
            ShowErrorMessage(L"Error playing this file.", hr);
          }
          printf("OnMediaItemCreated\n");
          fflush(stdout);
          continue;
        } else if (msg.message == WM_PLAYER_ITEMSET) {
          HRESULT hr = S_OK;

          // The media item was set successfully.

          hr = pPlayer->Play();

          if (FAILED(hr)) {
            ShowErrorMessage(L"IMFPMediaPlayer::Play failed.", hr);
          }
          printf("OnMediaItemSet\n");
          fflush(stdout);
          continue;
        }

        DispatchMessageW(&msg);
      }

      CoUninitialize();

      printf("media exit\n");
    }) {}

  void join() {
    if (comThread_.joinable()) {
      PostThreadMessageW(
          GetThreadId(comThread_.native_handle()), WM_QUIT, 0, 0L);
      comThread_.join();
    }
  }

  void Click() {
    PostThreadMessageW(
        GetThreadId(comThread_.native_handle()), WM_PLAYER_CLICK, 0, 0L);
  }

  void ShowErrorMessage(PCWSTR format, HRESULT hrErr) {
    PostThreadMessageW(
        GetThreadId(comThread_.native_handle()),
        WM_PLAYER_SHOWERROR,
        (WPARAM)hrErr,
        (LPARAM)format);
  }

  void OnMediaItemCreated(MFP_MEDIAITEM_CREATED_EVENT* pEvent) {
    pEvent->pMediaItem->AddRef();
    PostThreadMessageW(
        GetThreadId(comThread_.native_handle()),
        WM_PLAYER_ITEMCREATED,
        0,
        (LPARAM)pEvent->pMediaItem);
  }

  void OnMediaItemSet(MFP_MEDIAITEM_SET_EVENT* /*pEvent*/) {
    PostThreadMessageW(
        GetThreadId(comThread_.native_handle()), WM_PLAYER_ITEMSET, 0, 0L);
  }

};

template<typename Fn>
struct kbdhookstate {
  Fn& fn_;
  unifex::inplace_stop_token token_;
  HHOOK hHook_;
  std::thread msgThread_;

  static inline std::atomic<kbdhookstate<Fn>*> self_{nullptr};

  explicit kbdhookstate(Fn& fn, unifex::inplace_stop_token token)
    : fn_(fn)
    , token_(token)
    , hHook_(NULL)
    , msgThread_(
          [](kbdhookstate<Fn>* self) noexcept {
            kbdhookstate<Fn>* empty = nullptr;
            if (!kbdhookstate<Fn>::self_.compare_exchange_strong(
                    empty, self)) {
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

            kbdhookstate<Fn>* expired = self;
            if (!kbdhookstate<Fn>::self_.compare_exchange_strong(
                    expired, nullptr)) {
              std::terminate();
            }

            printf("keyboard hook removed\n");
          },
          this) {
  }

  void join() {
    if (msgThread_.joinable()) {
      PostThreadMessageW(
          GetThreadId(msgThread_.native_handle()), WM_QUIT, 0, 0L);
      try {
        if (msgThread_.joinable()) {
          msgThread_.join();
        }
      } catch (...) {
      }
    }
  }

  static
  LRESULT CALLBACK
  KbdHookProc(_In_ int nCode, _In_ WPARAM wParam, _In_ LPARAM lParam) {
    kbdhookstate<Fn>* self =
        kbdhookstate<Fn>::self_.load();
    if (!!self && nCode >= 0 &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
      self->fn_(wParam);
      return CallNextHookEx(self->hHook_, nCode, wParam, lParam);
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
  }
};

auto keyboard_events(unifex::inplace_stop_token token) {
  return create_event_sender_range<WPARAM>(
      token,
      [&](auto& fn) noexcept {
        return kbdhookstate<decltype(fn)>{fn, token};
      },
      [](auto& r) noexcept { r.join(); });
}

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
      printf("\n"); // end the line of '.'
      stop_.load()->request_stop();
    }
    return TRUE;
  }
};

using next_t = decltype(*keyboard_events(std::declval<unifex::inplace_stop_token>()).begin());
auto with_stop_token(next_t& next, unifex::inplace_stop_token token) {
  return unifex::with_query_value(next, unifex::get_stop_token, token);
}

int wmain() {
  unifex::timed_single_thread_context context;
  unifex::inplace_stop_source stopSource;
  clean_stop exit{stopSource};

  Player player;

  for (auto next : keyboard_events(stopSource.get_token())) {
    auto evt = unifex::sync_wait(with_stop_token(next, stopSource.get_token()));
    if (!evt) {
      break;
    }
    player.Click();
  }

  player.join();

  printf("main exit\n");
}
