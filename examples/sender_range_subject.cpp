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
#include <unifex/then.hpp>
#include <unifex/let_value.hpp>
#include <unifex/repeat_effect_until.hpp>
//#include <unifex/range.hpp>
#include <ranges>

namespace rng = std::ranges;

using namespace unifex;
using namespace std::literals::chrono_literals;

template<typename EventType, typename RegisterFn, typename UnregisterFn>
struct event_sender_range_factory {
  ~event_sender_range_factory() {}

  struct event_function {
      event_sender_range_factory* factory_;
      template<typename EventType2>
      void operator()(EventType2&& event){
          void* op = factory_->pendingOperation_.exchange(nullptr);
          if (op) {
              complete_function_t complete_with_event = nullptr;
              while (!complete_with_event) {
                  complete_with_event = factory_->complete_with_event_.exchange(nullptr);
              }
              complete_with_event(op, (EventType2&&)event);
          } // else discard this event
      }
  };

  using registration_t = callable_result_t<RegisterFn, event_function&>;
  RegisterFn registerFn_;
  UnregisterFn unregisterFn_;
  union storage {
    ~storage() {}
    int empty_{0};
    registration_t registration_;
  } storage_;
  std::atomic<void*> pendingOperation_{nullptr};
  using complete_function_t = void(*)(void*, EventType) noexcept;
  std::atomic<complete_function_t> complete_with_event_{nullptr};
  event_function event_function_{this};

  template<typename StopToken, typename Receiver>
  struct event_operation_state {
      event_sender_range_factory* factory_;
      StopToken stopToken_;
      Receiver receiver_;

      static void complete_with_event(void* selfVoid, EventType event) noexcept {
          auto& self = *reinterpret_cast<event_operation_state*>(selfVoid);
          set_value(std::move(self.receiver_), std::move(event));
      }

      void start() & noexcept {
          void* expectedOperation = nullptr;
          if (!factory_->pendingOperation_.compare_exchange_strong(expectedOperation, static_cast<void*>(this))) {
              std::terminate();
          }
          complete_function_t expectedComplete = nullptr;
          if(!factory_->complete_with_event_.compare_exchange_strong(expectedComplete, &complete_with_event)) {
              std::terminate();
          }
      }
  };
  template<typename StopToken>
  struct event_sender {
      event_sender_range_factory* factory_;
      StopToken stopToken_;

      template<template<typename...> class Variant, template<typename...> class Tuple>
      using value_types = Variant<Tuple<EventType>>;

      template<template<typename...> class Variant>
      using error_types = Variant<>;

      static constexpr bool sends_done = true;

      template<typename Receiver>
      event_operation_state<StopToken, Receiver> connect(Receiver&& receiver) {
          return {factory_, stopToken_, (Receiver&&)receiver};
      }
  };

  template<typename Range>
  struct sender_range {
      event_sender_range_factory* factory_;
      Range range_;
      ~sender_range() noexcept {
          factory_->unregisterFn_(factory_->storage_.registration_);
          factory_->storage_.registration_.~registration_t();
      }

    //   using value_type = typename Range::value_type;

    //   using iterator = typename Range::iterator;
    //   using const_iterator = typename Range::const_iterator;

      auto begin() {return range_.begin();}
      auto end() {return range_.end();}
  };

  template<typename StopToken>
  auto start(StopToken token) {
      new(&storage_.registration_) registration_t(registerFn_(event_function_));
      auto result = rng::views::iota(0) 
        | rng::views::transform(
            [this, token](int ){
                return event_sender<StopToken>{this, token};
            });
      return sender_range<decltype(result)>{this, std::move(result)};
  }
};

template<typename EventType, typename RegisterFn, typename UnregisterFn>
event_sender_range_factory<EventType, RegisterFn, UnregisterFn> 
create_event_sender_range(RegisterFn&& registerFn, UnregisterFn&& unregisterFn) {
    using result_t = event_sender_range_factory<EventType, RegisterFn, UnregisterFn>;
    static_assert(is_nothrow_callable_v<RegisterFn, typename result_t::event_function&>, "register function must be noexcept");
    static_assert(is_nothrow_callable_v<UnregisterFn, callable_result_t<RegisterFn, typename result_t::event_function&>&>, "unregister function must be noexcept");
    return {(RegisterFn&&)registerFn, (UnregisterFn&&)unregisterFn, {0}};
}


timed_single_thread_context context;
inplace_stop_source stopSource;

struct event {};

template<typename Fn>
struct event_registration {
    struct receiver{
    private:
        template<typename CPO, typename... As>
        friend void tag_invoke(CPO, receiver&&, As&&...) noexcept {}
        template<typename CPO, typename... As>
        friend void tag_invoke(CPO, const receiver&, As&&...) noexcept;
    };
    static auto register_for_events(Fn& fn) noexcept {
        return let_value(schedule(context.get_scheduler()), []{
            return schedule_after(context.get_scheduler(), 1s);
        })
        | then([&fn](){fn(event{});}) 
        | repeat_effect() 
        | with_query_value(get_stop_token, stopSource.get_token());
    }
    using op_t = connect_result_t<callable_result_t<decltype(&register_for_events), Fn&>, receiver>;
    op_t op_;
    ~event_registration() noexcept {unregister();}
    explicit event_registration(Fn& fn) noexcept : op_(connect(register_for_events(fn), receiver{})) {
        start(op_);
    }

    void unregister() noexcept {stopSource.request_stop(); std::this_thread::sleep_for(200ms); /*need to wait for expression to complete*/}

    event_registration() = delete;
    event_registration(const event_registration&) = delete;
    event_registration(event_registration&&) = delete;
};

int main() {

  auto eventRangeFactory = create_event_sender_range<event>(
      [](auto& fn) noexcept {return event_registration<decltype(fn)>{fn};}, 
      [](auto& r)noexcept{r.unregister();});

  int remaining = 5;
  for (auto next : eventRangeFactory.start(stopSource.get_token())) {
      auto evt = sync_wait(next);
      printf("."); fflush(stdout);
      if (--remaining == 0) { break; }
      (void)evt;
  }
  printf("\nexit\n");

}