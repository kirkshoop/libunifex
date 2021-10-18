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

#include <unifex/just.hpp>
#include <unifex/let_value.hpp>
#include <unifex/repeat_effect_until.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/stop_when.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>
#include <unifex/timed_single_thread_context.hpp>
#include <unifex/with_query_value.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <ranges>

// using namespace unifex;
using namespace std::literals::chrono_literals;

template <typename EventType, typename RegisterFn, typename UnregisterFn>
struct event_sender_range_factory {
  ~event_sender_range_factory() {}

  struct event_function {
    event_sender_range_factory* factory_;
    template <typename EventType2>
    void operator()(EventType2&& event) {
      void* op = factory_->pendingOperation_.exchange(nullptr);
      if (op) {
        complete_function_t complete_with_event = nullptr;
        while (!complete_with_event) {
          complete_with_event =
              factory_->complete_with_event_.exchange(nullptr);
        }
        complete_with_event(op, &event);
      }  // else discard this event
    }
  };

  using registration_t = unifex::callable_result_t<RegisterFn, event_function&>;
  RegisterFn registerFn_;
  UnregisterFn unregisterFn_;
  std::atomic_bool registered_{false};
  union storage {
    ~storage() {}
    int empty_{0};
    registration_t registration_;
  } storage_;
  std::atomic<void*> pendingOperation_{nullptr};
  using complete_function_t = void (*)(void*, EventType*) noexcept;
  std::atomic<complete_function_t> complete_with_event_{nullptr};
  event_function event_function_{this};

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
      unregisterFn_(storage_.registration_);
      storage_.registration_.~registration_t();
      stop_pending();
    }
  }

  template <typename StopToken, typename Receiver>
  struct event_operation_state {
    using receiverToken = unifex::callable_result_t<
        unifex::tag_t<unifex::get_stop_token>,
        const Receiver&>;
    event_sender_range_factory* factory_;
    StopToken stopToken_;
    Receiver receiver_;
    receiverToken receiverToken_;
    struct stop_callback {
      event_sender_range_factory* factory_;
      void operator()() { factory_->stop_pending(); }
    };
    typename StopToken::template callback_type<stop_callback> callback_;

    template <typename Receiver2>
    event_operation_state(
        event_sender_range_factory* factory,
        StopToken stopToken,
        Receiver2&& receiver)
      : factory_(factory)
      , stopToken_(stopToken)
      , receiver_((Receiver2 &&) receiver)
      , receiverToken_(unifex::get_stop_token(receiver_))
      , callback_(receiverToken_, stop_callback{factory_}) {}

    static void complete_with_event(void* selfVoid, EventType* event) noexcept {
      auto& self = *reinterpret_cast<event_operation_state*>(selfVoid);
      if (!!event) {
        unifex::set_value(std::move(self.receiver_), std::move(*event));
      } else {
        unifex::set_done(std::move(self.receiver_));
      }
    }

    void start() & noexcept {
      if (stopToken_.stop_requested() || receiverToken_.stop_requested()) {
        unifex::set_done(std::move(receiver_));
      } else {
        void* expectedOperation = nullptr;
        if (!factory_->pendingOperation_.compare_exchange_strong(
                expectedOperation, static_cast<void*>(this))) {
          std::terminate();
        }
        complete_function_t expectedComplete = nullptr;
        if (!factory_->complete_with_event_.compare_exchange_strong(
                expectedComplete, &complete_with_event)) {
          std::terminate();
        }
      }
    }
  };
  template <typename StopToken>
  struct event_sender {
    event_sender_range_factory* factory_;
    StopToken stopToken_;

    template <
        template <typename...>
        class Variant,
        template <typename...>
        class Tuple>
    using value_types = Variant<Tuple<EventType>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    template <typename Receiver>
    event_operation_state<StopToken, Receiver> connect(Receiver&& receiver) {
      return {factory_, stopToken_, (Receiver &&) receiver};
    }
  };

  template <typename Range, typename StopToken>
  struct sender_range {
    struct stop_callback {
      event_sender_range_factory* factory_;
      void operator()() { factory_->unregister(); }
    };
    event_sender_range_factory* factory_;
    Range range_;
    typename StopToken::template callback_type<stop_callback> callback_;
    ~sender_range() noexcept { factory_->unregister(); }
    template <typename Range2>
    explicit sender_range(
        event_sender_range_factory* factory, Range2&& range, StopToken token)
      : factory_(factory)
      , range_((Range2 &&) range)
      , callback_(token, stop_callback{factory_}) {}

    auto begin() { return range_.begin(); }
    auto end() { return range_.end(); }
  };

  template <typename StopToken>
  auto start(StopToken token) {
    bool unregistered = false;
    if (!registered_.compare_exchange_strong(unregistered, true)) {
      std::terminate();
    }
    new ((void*)&storage_.registration_)
        registration_t(registerFn_(event_function_));
    auto ints = std::views::iota(0);
    auto senderFactory = std::views::transform([this, token](int) {
      return event_sender<StopToken>{this, token};
    });
    using rangeOfSenders =
        sender_range<decltype(ints | senderFactory), StopToken>;
    return rangeOfSenders{this, ints | senderFactory, token};
  }
};

template <typename EventType, typename RegisterFn, typename UnregisterFn>
event_sender_range_factory<EventType, RegisterFn, UnregisterFn>
create_event_sender_range(
    RegisterFn&& registerFn, UnregisterFn&& unregisterFn) {
  using result_t =
      event_sender_range_factory<EventType, RegisterFn, UnregisterFn>;
  static_assert(
      unifex::
          is_nothrow_callable_v<RegisterFn, typename result_t::event_function&>,
      "register function must be noexcept");
  static_assert(
      unifex::is_nothrow_callable_v<
          UnregisterFn,
          unifex::callable_result_t<
              RegisterFn,
              typename result_t::event_function&>&>,
      "unregister function must be noexcept");
  return {(RegisterFn &&) registerFn, (UnregisterFn &&) unregisterFn, {0}};
}

#include <Shlwapi.h>
#include <mferror.h>
#include <mfplay.h>
#include <shobjidl.h>  // defines IFileOpenDialog
#include <strsafe.h>
#include <windows.h>
#include <windowsx.h>
#include <winuser.h>

#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "shlwapi.lib")

struct Player;

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
  void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader);
};

struct Player {
  struct Release {
    void operator()(IUnknown* pUnknown) {
      // if (pUnknown)
      //  pUnknown->Release();
    }
  };
  // Application callback object.
  std::unique_ptr<MediaPlayerCallback> pPlayerCB;
  // The MFPlay player object.
  std::unique_ptr<IMFPMediaPlayer, Release> pPlayer;

  explicit Player(int i = 1)
    : pPlayerCB(new MediaPlayerCallback(this))
    , pPlayer(nullptr) {
    IMFPMediaPlayer* player = nullptr;
    HRESULT hr = MFPCreateMediaPlayer(
        NULL,
        FALSE,            // Start playback automatically?
        0,                // Flags
        pPlayerCB.get(),  // Callback pointer
        NULL,             // Video window
        &player);
    if (FAILED(hr)) {
      std::terminate();
    }
    pPlayer.reset(player);

    // Create a new media item for this URL.
    hr = pPlayer->CreateMediaItemFromURL(
        std::format(L"https://webwit.nl/input/kbsim/mp3/{}_.mp3", i).c_str(),
        FALSE,
        0,
        NULL);
    if (FAILED(hr)) {
      std::terminate();
    }
  }

  void Click() {
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
  }

  void ShowErrorMessage(PCWSTR format, HRESULT hrErr) {
    HRESULT hr = S_OK;
    WCHAR msg[MAX_PATH];

    hr = StringCbPrintfW(msg, sizeof(msg), L"%s (hr=0x%X)", format, hrErr);

    if (SUCCEEDED(hr)) {
      MessageBoxW(NULL, msg, L"Error", MB_ICONERROR);
    }
  }

  void OnMediaItemCreated(MFP_MEDIAITEM_CREATED_EVENT* pEvent) {
    HRESULT hr = S_OK;

    // The media item was created successfully.

    if (pPlayer) {
      // Set the media item on the player. This method completes asynchronously.
      hr = pPlayer->SetMediaItem(pEvent->pMediaItem);
    }

    if (FAILED(hr)) {
      ShowErrorMessage(L"Error playing this file.", hr);
    }
  }

  void OnMediaItemSet(MFP_MEDIAITEM_SET_EVENT* /*pEvent*/) {
    HRESULT hr = S_OK;

    hr = pPlayer->Play();

    if (FAILED(hr)) {
      ShowErrorMessage(L"IMFPMediaPlayer::Play failed.", hr);
    }
  }
};

void STDMETHODCALLTYPE
MediaPlayerCallback::OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader) {
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

template <typename Fn, typename StopToken>
struct kbdhookstate {
  Fn& fn_;
  StopToken token_;
  HHOOK hHook_;
  std::thread msgThread_;

  static inline std::atomic<kbdhookstate*> self_{nullptr};

  static void HookFn(kbdhookstate* self) noexcept {
    kbdhookstate* empty = nullptr;
    if (!self_.compare_exchange_strong(empty, self)) {
      std::terminate();
    }

    self->hHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &KbdHookProc, NULL, NULL);
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

    kbdhookstate* expired = self;
    if (!self_.compare_exchange_strong(expired, nullptr)) {
      std::terminate();
    }
  }

  explicit kbdhookstate(Fn& fn, StopToken token)
    : fn_(fn)
    , token_(token)
    , hHook_(NULL)
    , msgThread_(&HookFn, this) {}

  void join() {
    if (msgThread_.joinable()) {
      PostThreadMessageW(
          GetThreadId(msgThread_.native_handle()), WM_QUIT, 0, 0L);
      msgThread_.join();
    }
  }

  static LRESULT CALLBACK
  KbdHookProc(_In_ int nCode, _In_ WPARAM wParam, _In_ LPARAM lParam) {
    kbdhookstate* self = self_.load();
    if (!!self && nCode >= 0 &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
      self->fn_(wParam);
      return CallNextHookEx(self->hHook_, nCode, wParam, lParam);
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
  }
};

constexpr auto with_stop_token = [](auto token) {
  return unifex::with_query_value(unifex::get_stop_token, token);
};

int wmain() {
  unifex::timed_single_thread_context context;
  unifex::inplace_stop_source stopSource;
  unifex::inplace_stop_token stopToken = stopSource.get_token();

  if (FAILED(CoInitializeEx(
          NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
    return 0;
  }

  Player player;

  auto eventRangeFactory = create_event_sender_range<WPARAM>(
      [=](auto& fn) noexcept {
        return kbdhookstate{fn, stopToken};
      },
      [](auto& r) noexcept { r.join(); });

  unsigned i = 0;
   for (auto next : eventRangeFactory.start(stopToken)) {
    auto evt = unifex::sync_wait(
        next                          //
        | with_stop_token(stopToken)  //
        | unifex::stop_when(          //
              unifex::repeat_effect(  //
                  unifex::schedule()  //
                  | unifex::then([] {
                      MSG msg{};
                      while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                        DispatchMessageW(&msg);
                    }))));
    if (!evt) {
      break;
    }
    player.Click();
  }

  printf("\nexit\n");
}