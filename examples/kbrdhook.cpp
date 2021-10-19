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

//using namespace unifex;
using namespace std::literals::chrono_literals;

template <
    typename EventType,
    typename RangeStopToken,
    typename RegisterFn,
    typename UnregisterFn>
struct sender_range {
  struct event_function {
    sender_range* scope_;
    explicit event_function(sender_range* scope) : scope_(scope) {}
    template <typename EventType2>
    void operator()(EventType2&& event) {
      void* op = scope_->pendingOperation_.exchange(nullptr);
      if (op) {
        complete_function_t complete_with_event = nullptr;
        while (!complete_with_event) {
          complete_with_event =
              scope_->complete_with_event_.exchange(nullptr);
        }
        complete_with_event(op, &event);
      }  // else discard this event
    }
  };

  using complete_function_t = void (*)(void*, EventType*) noexcept;

  template <typename State>
  void start(State* state) noexcept {
    if (rangeToken_.stop_requested() ||
        state->eventStopToken_.stop_requested()) {
      unifex::set_done(std::move(state->rec_));
    } else {
      void* expectedOperation = nullptr;
      if (!pendingOperation_.compare_exchange_strong(
              expectedOperation, static_cast<void*>(state))) {
        std::terminate();
      }
      complete_function_t expectedComplete = nullptr;
      if (!complete_with_event_.compare_exchange_strong(
              expectedComplete, state->complete_with_event())) {
        std::terminate();
      }
    }
  }

  using registration_t =
    unifex::callable_result_t<RegisterFn, event_function&>;

  void stop_pending() {
    void* op = pendingOperation_.exchange(nullptr);
    if (op) {
      complete_function_t complete_with_event = nullptr;
      while (!complete_with_event) {
        complete_with_event = complete_with_event_.exchange(nullptr);
      }
      complete_with_event(op, nullptr);
    }
  }

  void unregister() {
    bool registered = true;
    if (registered_.compare_exchange_strong(registered, false)) {
      unregisterFn_(registration_.get());
      registration_.destruct();
      stop_pending();
    }
  }


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
      complete_function_t complete_with_event() {
        return &_complete_with_event;
      }
      sender_range* scope_;
      Receiver& rec_;
      EventStopToken eventStopToken_;
      struct stop_callback {
        sender_range* scope_;
        void operator()() noexcept { scope_->stop_pending(); }
      };
      typename EventStopToken::template callback_type<stop_callback> callback_;
      state(sender_range* scope, Receiver& rec, EventStopToken eventStopToken)
        : scope_(scope)
        , rec_(rec)
        , eventStopToken_(eventStopToken) 
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
    void operator()() noexcept { scope_->unregister(); }
  };

  static auto make_range(sender_range* self) {
    return ::ranges::views::iota(0)
    | ::ranges::views::transform(
        [self](int) { return unifex::create(create_sender{}, self); });
  }

  unifex::callable_result_t<decltype(&make_range), sender_range*> range_;
  // args
  RangeStopToken rangeToken_;
  RegisterFn registerFn_;
  UnregisterFn unregisterFn_;
  // cancellation
  typename RangeStopToken::template callback_type<stop_callback> callback_;
  // tracking result of registerFn
  std::atomic_bool registered_;
  unifex::manual_lifetime<registration_t> registration_;
  // type-erased registration of a sender waiting for an event
  std::atomic<void*> pendingOperation_;
  std::atomic<complete_function_t> complete_with_event_;
  // fixed storage for the fucntion used to emit an event (allows event_function& to have the right lifetime)
  event_function event_function_;

public:
  ~sender_range() noexcept { unregister(); }
  explicit sender_range(RangeStopToken token, RegisterFn registerFn, UnregisterFn unregisterFn)
    : range_(make_range(this))
    , rangeToken_(token)
    , registerFn_(registerFn)
    , unregisterFn_(unregisterFn) 
    , callback_(rangeToken_, stop_callback{this}) 
    , registered_(false) 
    , pendingOperation_(nullptr)
    , complete_with_event_(nullptr)
    , event_function_(this) {
    bool unregistered = false;
    if (!registered_.compare_exchange_strong(unregistered, true)) {
      std::terminate();
    }
    registration_.construct_with(
        [this]() noexcept { return registerFn_(event_function_); });
  }
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
#include <mferror.h>
#include <shobjidl.h>   // defines IFileOpenDialog
#include <Shlwapi.h>
#include <strsafe.h>

#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "shlwapi.lib")

struct Player;

class MediaPlayerCallback : public IMFPMediaPlayerCallback
{
    Player* player_;
    long m_cRef; // Reference count

public:

    explicit MediaPlayerCallback(Player* player) : player_(player), m_cRef(1)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        static const QITAB qit[] = 
        {
            QITABENT(MediaPlayerCallback, IMFPMediaPlayerCallback),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }
    STDMETHODIMP_(ULONG) AddRef() 
    {
            return InterlockedIncrement(&m_cRef); 
    }
    STDMETHODIMP_(ULONG) Release()
    {
        ULONG count = InterlockedDecrement(&m_cRef);
        if (count == 0)
        {
            delete this;
            return 0;
        }
        return count;
    }

    // IMFPMediaPlayerCallback methods
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER *pEventHeader);
};

struct Player {
    IMFPMediaPlayer         *pPlayer = NULL;      // The MFPlay player object.
    MediaPlayerCallback     *pPlayerCB = NULL;    // Application callback object.

    Player() : pPlayer(nullptr), pPlayerCB(new MediaPlayerCallback(this)) {
        HRESULT hr = MFPCreateMediaPlayer(
            NULL,
            FALSE,          // Start playback automatically?
            0,              // Flags
            pPlayerCB,      // Callback pointer
            NULL,           // Video window
            &pPlayer
            );
        if(FAILED(hr)) {
            std::terminate();
        }

        // Create a new media item for this URL.
        hr = pPlayer->CreateMediaItemFromURL(L"https://webwit.nl/input/kbsim/mp3/1_.mp3", FALSE, 0, NULL);
        if(FAILED(hr)) {
            std::terminate();
        }
    }

    void Click() {
        HRESULT hr = pPlayer->Stop(); 
        if (FAILED(hr)) {std::terminate();}
        hr = pPlayer->Play(); 
        if (FAILED(hr)) {std::terminate();}

        printf(".");
        fflush(stdout);
    }

    void ShowErrorMessage(PCWSTR format, HRESULT hrErr) {
        HRESULT hr = S_OK;
        WCHAR msg[MAX_PATH];

        hr = StringCbPrintfW(msg, sizeof(msg), L"%s (hr=0x%X)", format, hrErr);

        if (SUCCEEDED(hr))
        {
            MessageBoxW(NULL, msg, L"Error", MB_ICONERROR);
        }
    }

    void OnMediaItemCreated(MFP_MEDIAITEM_CREATED_EVENT *pEvent) {
        HRESULT hr = S_OK;

        // The media item was created successfully.

        if (pPlayer)
        {
            // Set the media item on the player. This method completes asynchronously.
            hr = pPlayer->SetMediaItem(pEvent->pMediaItem);
        }

        if (FAILED(hr))
        {
            ShowErrorMessage(L"Error playing this file.", hr);
        }
    }

    void OnMediaItemSet(MFP_MEDIAITEM_SET_EVENT * /*pEvent*/) 
    {
        HRESULT hr = S_OK;

        hr = pPlayer->Play();

        if (FAILED(hr))
        {
            ShowErrorMessage(L"IMFPMediaPlayer::Play failed.", hr);
        }
    }
};

void STDMETHODCALLTYPE MediaPlayerCallback::OnMediaPlayerEvent(MFP_EVENT_HEADER *pEventHeader)
{
    if (FAILED(pEventHeader->hrEvent)) {
    player_->ShowErrorMessage(L"Playback error", pEventHeader->hrEvent);
    return;
    }

    switch (pEventHeader->eEventType)
    {
    case MFP_EVENT_TYPE_MEDIAITEM_CREATED:
        player_->OnMediaItemCreated(MFP_GET_MEDIAITEM_CREATED_EVENT(pEventHeader));
        break;

    case MFP_EVENT_TYPE_MEDIAITEM_SET:
        player_->OnMediaItemSet(MFP_GET_MEDIAITEM_SET_EVENT(pEventHeader));
        break;
    }
}

template<typename Fn, typename StopToken>
struct kbdhookstate {
  Fn& fn_;
  StopToken token_;
  HHOOK hHook_;
  std::thread msgThread_;

  static inline std::atomic<kbdhookstate<Fn, StopToken>*> self_{nullptr};

  explicit kbdhookstate(Fn& fn, StopToken token)
    : fn_(fn)
    , token_(token)
    , hHook_(NULL)
    , msgThread_(
          [](kbdhookstate<Fn, StopToken>* self) noexcept {
            kbdhookstate<Fn, StopToken>* empty = nullptr;
            if (!kbdhookstate<Fn, StopToken>::self_.compare_exchange_strong(
                    empty, self)) {
              std::terminate();
            }

            self->hHook_ = SetWindowsHookExW(
                WH_KEYBOARD_LL,
                &KbdHookProc,
                NULL,
                NULL);
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
            while (GetMessageW(&msg, NULL, 0, 0) && !self->token_.stop_requested()) {
              DispatchMessageW(&msg);
            }

            bool result = UnhookWindowsHookEx(std::exchange(self->hHook_, (HHOOK)NULL));
            if (!result) {
              std::terminate();
            }

            kbdhookstate<Fn, StopToken>* expired = self;
            if (!kbdhookstate<Fn, StopToken>::self_.compare_exchange_strong(
                    expired, nullptr)) {
              std::terminate();
            }
          },
          this) {
  }

  void join() {
    if (msgThread_.joinable()) {
      PostThreadMessageW(
          GetThreadId(msgThread_.native_handle()), WM_QUIT, 0, 0L);
      msgThread_.join();
    }
  }

  static
  LRESULT CALLBACK
  KbdHookProc(_In_ int nCode, _In_ WPARAM wParam, _In_ LPARAM lParam) {
    kbdhookstate<Fn, StopToken>* self =
        kbdhookstate<Fn, StopToken>::self_.load();
    if (!!self && nCode >= 0 &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
      self->fn_(wParam);
      return CallNextHookEx(self->hHook_, nCode, wParam, lParam);
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
  }
};

template<typename StopToken>
auto keyboard_events(StopToken token) {
  return create_event_sender_range<WPARAM>(
      token,
      [&](auto& fn) noexcept {
        return kbdhookstate<decltype(fn), StopToken>{fn, token};
      },
      [](auto& r) noexcept { r.join(); });
}

int wmain() {
  unifex::timed_single_thread_context context;
  unifex::inplace_stop_source stopSource;

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
    {
        return 0;
    }

    Player player;

    for (auto next : keyboard_events(stopSource.get_token())) {
        auto evt = unifex::sync_wait(
            unifex::stop_when(
                unifex::with_query_value(next, unifex::get_stop_token, stopSource.get_token()),
                unifex::repeat_effect(
                    unifex::then(unifex::schedule(), []{
                            MSG msg{};
                            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)){
                              DispatchMessageW(&msg);
                            }
                }))
            ));
        if (!evt) {
            break;
        }
        player.Click();
    }

    printf("\nexit\n");

}