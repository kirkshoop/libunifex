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

#include <mfplay.h>
#include <windows.h>
#include <windowsx.h>
#include <winuser.h>
#pragma comment(lib, "mfplay.lib")
#include <Shlwapi.h>
#include <mferror.h>
#include <shobjidl.h>  // defines IFileOpenDialog
#pragma comment(lib, "shlwapi.lib")
#include <strsafe.h>

#include <comip.h>

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
        unifex::scope_guard releasePlayer{ [&]() noexcept {
          if (!!pPlayer) {
            std::exchange(pPlayer, nullptr)->Release();
          }
        } };
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
            }
            else if (msg.message == WM_PLAYER_SHOWERROR) {
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
            }
            else if (msg.message == WM_PLAYER_ITEMCREATED) {
                HRESULT hr = S_OK;
                IMFPMediaItem* pMediaItem = (IMFPMediaItem*)msg.lParam;
                unifex::scope_guard releaseItem{ [&]() noexcept {
                  if (!!pMediaItem) {
                    std::exchange(pMediaItem, nullptr)->Release();
                  }
                } };

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
            }
            else if (msg.message == WM_PLAYER_ITEMSET) {
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
