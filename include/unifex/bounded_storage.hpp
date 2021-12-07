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

#include <unifex/config.hpp>

#include <unifex/blocking.hpp>
#include <unifex/manual_lifetime.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/storage_concepts.hpp>
#include <unifex/variant_tail_sender.hpp>

#include <unifex/detail/prologue.hpp>

namespace unifex {

// example of bounded storage. stores a std::array of storage.
// This will not complete a construct() sender unless there is an
// unused item in the array
// a destroy() sender will either return the storage to the free
// stack or will pop a construct op off of the pending queue and
// complete it with the storage being destructed
template <std::size_t Limit>
struct bounded_storage {
  struct _any_stg_ref {
    void* stg_;
    std::size_t idx_;
    void* v_;
    template <typename T>
    T& get() const {
      return *reinterpret_cast<T*>(v_);
    }
  };
  using any_ref_type = _any_stg_ref;

  template <typename TagPack>
  struct _stg;
  template <typename T>
  struct _stg<void(T)>
    : tag_invoke_member_base_t<
          _stg<void(T)>,
          tag_t<construct>,
          tag_t<destruct>> {
    using start_fn_t = void (*)(void*, size_t idx) noexcept;
    struct pending {
      pending* next_ = nullptr;
      void* op_ = nullptr;
      start_fn_t start_ = nullptr;
    };
    struct storage_state {
      unifex::manual_lifetime<T> op_{};
      std::atomic<pending*> current_{nullptr};
    };
    std::array<storage_state, Limit> storage_;
    std::atomic<pending*> pending_;
    _stg() = default;
    _stg(_stg&&) = delete;
    _stg(const _stg&) = delete;
    _stg& operator=(_stg&&) = delete;
    _stg& operator=(const _stg&) = delete;

    struct stg_ref {
      explicit stg_ref(_stg* stg, size_t idx) : stg_(stg), idx_(idx) {}
      stg_ref(const stg_ref&) = default;
      stg_ref& operator=(const stg_ref&) = default;
      stg_ref(const any_ref_type& a)
        : stg_(reinterpret_cast<_stg*>(a.stg_))
        , idx_(a.idx_) {}
      _stg* stg_;
      std::size_t idx_;
      T& get() const { return stg_->storage_[idx_].op_.get(); }
      operator any_ref_type() const { return {stg_, idx_, &get()}; }
    };

    template <typename... ArgN>
    struct _cnstrct_sndr
      : tag_invoke_member_base_t<_cnstrct_sndr<ArgN...>, tag_t<blocking>> {
      template <
          template <typename...>
          class Variant,
          template <typename...>
          class Tuple>
      using value_types = Variant<Tuple<stg_ref>>;
      template <template <typename...> class Variant>
      using error_types = Variant<std::exception_ptr>;
      static inline constexpr bool sends_done = false;

      template <typename Receiver>
      struct _op : pending {
        _stg* stg_;
        Receiver r_;
        std::tuple<ArgN...> args_;
        std::size_t idx_ = 0;

        using result_t = variant_tail_sender<
            null_tail_sender,
            callable_result_t<
                unifex::tag_t<unifex::set_error>,
                Receiver,
                std::exception_ptr>,
            callable_result_t<
                unifex::tag_t<unifex::set_value>,
                Receiver,
                stg_ref>>;

        result_t start() noexcept {
          idx_ = 0;
          pending* empty = nullptr;
          // allocate storage attempt
          while (idx_ < Limit &&
                 !stg_->storage_[idx_].current_.compare_exchange_strong(
                     empty, this)) {
            ++idx_;
          }
          // no storage available
          if (idx_ == Limit) {
            // delay start until storage is available
            this->op_ = this;
            this->start_ = +[](void* self, size_t idx) noexcept {
              _op* op = reinterpret_cast<_op*>(self);
              op->idx_ = idx;
              try {
                std::apply(
                    [op](auto&&... argN) {
                      op->stg_->storage_[op->idx_].op_.construct(
                          (decltype(argN)&&)argN...);
                    },
                    std::move(op->args_));
                resume_tail_sender(result_or_null_tail_sender(
                    unifex::set_value,
                    std::move(op->r_),
                    stg_ref{op->stg_, op->idx_}));
              } catch (...) {
                resume_tail_sender(result_or_null_tail_sender(
                    unifex::set_error,
                    std::move(op->r_),
                    std::current_exception()));
              }
            };
            // push front (SHOULD BE push back)
            pending* expected = stg_->pending_.load();
            this->next_ = expected;
            while (!stg_->pending_.compare_exchange_strong(expected, this)) {
              std::this_thread::yield();
              expected = stg_->pending_.load();
              this->next_ = expected;
            }
            return null_tail_sender{};
          } else {
            try {
              std::apply(
                  [this](auto... argN) {
                    stg_->storage_[idx_].op_.construct(
                        (decltype(argN)&&)argN...);
                  },
                  std::move(args_));
              return result_or_null_tail_sender(
                  unifex::set_value, std::move(r_), stg_ref{stg_, idx_});
            } catch (...) {
              return result_or_null_tail_sender(
                  unifex::set_error, std::move(r_), std::current_exception());
            }
          }
        }
      };
      _stg* stg_;
      std::tuple<ArgN...> args_;
      constexpr unifex::blocking_kind blocking() const noexcept {
        return unifex::blocking_kind::maybe;
      }
      template <typename Receiver>
      _op<Receiver> connect(Receiver&& r) {
        return {{}, stg_, (Receiver &&) r, std::move(args_)};
      }
    };
    struct _dstrct_sndr
      : tag_invoke_member_base_t<_dstrct_sndr, tag_t<blocking>> {
      template <
          template <typename...>
          class Variant,
          template <typename...>
          class Tuple>
      using value_types = Variant<Tuple<>>;
      template <template <typename...> class Variant>
      using error_types = Variant<>;
      static inline constexpr bool sends_done = false;

      template <typename Receiver>
      struct _op {
        Receiver r_;
        _stg* stg_;
        stg_ref exp_;
        auto start() noexcept {
          // remove pending item
          pending* consume = stg_->pending_.load();
          while (!!consume &&
                 !stg_->pending_.compare_exchange_strong(
                     consume, consume->next_)) {
            std::this_thread::yield();
            consume = stg_->pending_.load();
          }
          // switch storage slot to new op or release the slot for a later
          // construct()
          stg_->storage_[exp_.idx_].op_.destruct();
          stg_->storage_[exp_.idx_].current_.exchange(consume);
          // start pending item
          if (!!consume) {
            consume->start_(consume->op_, exp_.idx_);
          }
          // complete op
          return unifex::set_value(std::move(r_));
        }
      };
      _stg* stg_;
      stg_ref exp_;
      constexpr unifex::blocking_kind blocking() const noexcept {
        return unifex::blocking_kind::always_inline;
      }
      template <typename Receiver>
      _op<Receiver> connect(Receiver&& r) {
        return {(Receiver &&) r, stg_, exp_};
      }
    };

    template <typename... ArgN>
    _cnstrct_sndr<ArgN...> construct(ArgN&&... argn) {
      return {{}, this, std::make_tuple((ArgN &&) argn...)};
    }

    _dstrct_sndr destruct(stg_ref exp) { return {{}, this, exp}; }
  };

  template(typename Tag)                    //
      (requires is_get_storage_for_v<Tag>)  //
      friend _stg<_strg::tag_pack_t<Tag>> tag_invoke(
          Tag, const bounded_storage&) {
    return {};
  }
};

}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
