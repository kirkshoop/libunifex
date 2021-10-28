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
#include <unifex/resume_tail_sender.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/then.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

TEST(TailSender, Smoke) {
  // basic concepts on null types
  EXPECT_TRUE(tail_receiver<null_tail_receiver>);
  EXPECT_TRUE(tail_sender<null_tail_sender>);
  EXPECT_TRUE(bool(
      tail_operation<connect_result_t<null_tail_sender, null_tail_receiver>>));

  auto fn = [](auto...) noexcept {
  };

  // as_tail_sender() only adapts non tail_sender types
  EXPECT_TRUE(bool(
      same_as<decltype(as_tail_sender(null_tail_sender{})), null_tail_sender>));
  EXPECT_TRUE(
      bool(!same_as<decltype(as_tail_sender(just())), decltype(just())>));
  EXPECT_TRUE(
      bool(!same_as<decltype(as_tail_sender(just(42))), decltype(just(42))>));
  EXPECT_TRUE(bool(!same_as<
                   decltype(as_tail_sender(just() | then(fn))),
                   decltype(just() | then(fn))>));

  // as_tail_sender() can adapt many senders to satisfy tail_sender
  EXPECT_TRUE(tail_sender<decltype(as_tail_sender(just()))>);
  EXPECT_TRUE(bool(tail_sender<decltype(as_tail_sender(just() | then(fn)))>));
  EXPECT_TRUE(bool(tail_sender<decltype(as_tail_sender(just(42) | then(fn)))>));
  EXPECT_TRUE(tail_sender<decltype(as_tail_sender(just(42)))>);

  resume_tail_sender(as_tail_sender(just() | then(fn)));

  int resume_count = 0;
  resume_tail_sender(
      as_tail_sender(just() | then([&]() noexcept { ++resume_count; })));
  EXPECT_EQ(resume_count, 1);
}

// Straight line - with conditional

struct C1 : tail_sender_base {
  template <typename Receiver>
  struct op : tail_operation_state_base {
    void start() noexcept {
      *ptr += "[C1]";
      unifex::set_value(std::move(r));
    }
    void unwind() noexcept {
      *ptr += "[~C1]";
      unifex::set_done(std::move(r));
    }
    std::string* ptr;
    Receiver r;
  };
  template <typename Receiver>
  op<Receiver> connect(Receiver r) noexcept {
    return {{}, ptr, r};
  }
  std::string* ptr;
};
static_assert(tail_sender_to<null_tail_sender, null_tail_receiver>);
static_assert(tail_sender_to<C1, null_tail_receiver>);

struct C2 : tail_sender_base {
  template <typename Receiver>
  struct op : tail_operation_state_base {
    [[nodiscard]] C1 start() noexcept {
      *ptr += "[C2]";
      unifex::set_value(std::move(r));
      return {{}, ptr};
    }
    void unwind() noexcept {
      *ptr += "[~C2]";
      unifex::set_done(std::move(r));
    }
    std::string* ptr;
    Receiver r;
  };
  template <typename Receiver>
  op<Receiver> connect(Receiver r) noexcept {
    return {{}, ptr, r};
  }
  std::string* ptr;
};
static_assert(tail_sender_to<C2, null_tail_receiver>);

struct C3 : tail_sender_base {
  template <typename Receiver>
  struct op : tail_operation_state_base {
    explicit operator bool() const noexcept {
      *ptr += "[==C3]";
      return ptr != nullptr;
    }
    [[nodiscard]] C2 start() noexcept {
      *ptr += "[C3]";
      unifex::set_value(std::move(r));
      return {{}, ptr};
    }
    void unwind() noexcept {
      *ptr += "[~C3]";
      unifex::set_done(std::move(r));
    }
    std::string* ptr;
    Receiver r;
  };
  template <typename Receiver>
  op<Receiver> connect(Receiver r) noexcept {
    return {{}, ptr, r};
  }
  std::string* ptr;
};
static_assert(tail_sender_to<C3, null_tail_receiver>);

struct C4 : tail_sender_base {
  template <typename Receiver>
  struct op : tail_operation_state_base {
    [[nodiscard]] C3 start() noexcept {
      *ptr += "[C4]";
      unifex::set_value(std::move(r));
      return {{}, ptr};
    }
    void unwind() noexcept {
      *ptr += "[~C4]";
      unifex::set_done(std::move(r));
    }
    std::string* ptr;
    Receiver r;
  };
  template <typename Receiver>
  op<Receiver> connect(Receiver r) noexcept {
    return {{}, ptr, r};
  }
  std::string* ptr;
};
static_assert(tail_sender_to<C4, null_tail_receiver>);

TEST(TailSender, Straight) {
  {
    std::string result;
    resume_tail_sender(C4{{}, &result}, null_tail_receiver{});
    std::cout << "result: " << result << "\n";
    EXPECT_EQ(result, "[C4][==C3][C3][C2][C1]");
  }

  {
    std::string result;
    { scoped_tail_sender destroyer(C4{{}, &result}); }
    std::cout << "result: " << result << "\n";
    EXPECT_EQ(result, "[~C4]");
  }
}

// Recursive - unrolled to loops
struct RC3;

struct RC1 : tail_sender_base {
  template <typename Receiver>
  struct op : tail_operation_state_base {
    RC3 start() noexcept;
    void unwind() noexcept {
      *ptr += "[~RC1]";
      unifex::set_done(std::move(r));
    }
    std::string* ptr;
    bool done;
    Receiver r;
  };
  template <typename Receiver>
  op<Receiver> connect(Receiver r) noexcept {
    return {{}, ptr, done, r};
  }
  std::string* ptr;
  bool done;
};

struct RC2 : tail_sender_base {
  template <typename Receiver>
  struct op : tail_operation_state_base {
    explicit operator bool() const noexcept {
      *ptr += "[==RC2]";
      return !done;
    }
    RC1 start() noexcept;
    void unwind() noexcept {
      *ptr += "[~RC2]";
      unifex::set_done(std::move(r));
    }
    std::string* ptr;
    bool done;
    Receiver r;
  };
  template <typename Receiver>
  op<Receiver> connect(Receiver r) noexcept {
    return {{}, ptr, done, r};
  }
  std::string* ptr;
  bool done;
};

struct RC3 : tail_sender_base {
  template <typename Receiver>
  struct op : tail_operation_state_base {
    RC2 start() noexcept {
      *ptr += "[RC3]";
      unifex::set_value(std::move(r));
      return {{}, ptr, done};
    }
    void unwind() noexcept {
      *ptr += "[~RC3]";
      unifex::set_done(std::move(r));
    }
    std::string* ptr;
    bool done;
    Receiver r;
  };
  template <typename Receiver>
  op<Receiver> connect(Receiver r) noexcept {
    return {{}, ptr, done, r};
  }
  std::string* ptr;
  bool done;
};

template <typename Receiver>
RC3 RC1::op<Receiver>::start() noexcept {
  *ptr += "[RC1]";
  unifex::set_value(std::move(r));
  return {{}, ptr, done};
}
template <typename Receiver>
RC1 RC2::op<Receiver>::start() noexcept {
  *ptr += "[RC2]";
  unifex::set_value(std::move(r));
  return {{}, ptr, true};
}
static_assert(tail_sender_to<RC1, null_tail_receiver>);
static_assert(tail_sender_to<RC2, null_tail_receiver>);
static_assert(tail_sender_to<RC3, null_tail_receiver>);

struct RC4 : tail_sender_base {
  template <typename Receiver>
  struct op : tail_operation_state_base {
    RC3 start() noexcept {
      *ptr += "[RC4]";
      unifex::set_value(std::move(r));
      return {{}, ptr, done};
    }
    void unwind() noexcept {
      *ptr += "[~RC4]";
      unifex::set_done(std::move(r));
    }
    std::string* ptr;
    bool done;
    Receiver r;
  };
  template <typename Receiver>
  op<Receiver> connect(Receiver r) noexcept {
    return {{}, ptr, done, r};
  }
  std::string* ptr;
  bool done;
};
static_assert(tail_sender_to<RC4, null_tail_receiver>);

struct RC5 : tail_sender_base {
  template <typename Receiver>
  struct op : tail_operation_state_base {
    RC4 start() noexcept {
      *ptr += "[RC5]";
      unifex::set_value(std::move(r));
      return {{}, ptr, done};
    }
    void unwind() noexcept {
      *ptr += "[~RC5]";
      unifex::set_done(std::move(r));
    }
    std::string* ptr;
    bool done;
    Receiver r;
  };
  template <typename Receiver>
  op<Receiver> connect(Receiver r) noexcept {
    return {{}, ptr, done, r};
  }
  std::string* ptr;
  bool done;
};
static_assert(tail_sender_to<RC5, null_tail_receiver>);

TEST(TailSender, Recursive) {
  {
    std::string result;
    resume_tail_sender(RC5{{}, &result, false}, null_tail_receiver{});
    std::cout << "result: " << result << "\n";
    EXPECT_EQ(result, "[RC5][RC4][RC3][==RC2][RC2][RC1][RC3][==RC2]");
  }

  {
    std::string result;
    { scoped_tail_sender destroyer(RC5{{}, &result, false}); }
    std::cout << "result: " << result << "\n";
    EXPECT_EQ(result, "[~RC5]");
  }
}

// Variant - forks in execution with loops
//
//           .---------.
//          |          |
//  *=5     V  pos     |
//  FC1 -> FC2 -> FC3 -' even
//        -1|      |odd
//          |neg   V
//          `---> FC4 ----> X
//                *-1
struct State {
  int x;
  std::string* ptr;

  struct FC3;

  struct FC4 : tail_sender_base {
    template <typename Receiver>
    struct op : tail_operation_state_base {
      void start() noexcept {
        EXPECT_TRUE(!!s);
        *s->ptr += "[FC4]";
        s->x = -s->x;
        unifex::set_value(std::move(r));
        std::exchange(s, nullptr);
      }
      void unwind() noexcept {
        EXPECT_TRUE(!!s);
        *s->ptr += "[~FC4]";
        unifex::set_done(std::move(r));
        std::exchange(s, nullptr);
      }
      State* s;
      Receiver r;
    };
    template <typename Receiver>
    op<Receiver> connect(Receiver r) noexcept {
      EXPECT_TRUE(!!s);
      return {{}, s, r};
    }
    State* s;
  };

  struct FC2 : tail_sender_base {
    template <typename Receiver>
    struct op : tail_operation_state_base {
      variant_tail_sender<FC3, FC4> start() noexcept {
        EXPECT_TRUE(!!s);
        *s->ptr += "[FC2]";
        s->x--;
        unifex::set_value(std::move(r));
        if (s->x >= 0)
          return FC3{{}, std::exchange(s, nullptr)};
        return FC4{{}, std::exchange(s, nullptr)};
      }
      void unwind() noexcept {
        EXPECT_TRUE(!!s);
        *s->ptr += "[~FC2]";
        unifex::set_done(std::move(r));
        std::exchange(s, nullptr);
      }
      State* s;
      Receiver r;
    };
    template <typename Receiver>
    op<Receiver> connect(Receiver r) noexcept {
      EXPECT_TRUE(!!s);
      return {{}, s, r};
    }
    State* s;
  };

  struct FC3 : tail_sender_base {
    template <typename Receiver>
    struct op : tail_operation_state_base {
      variant_tail_sender<FC2, FC4> start() noexcept {
        EXPECT_TRUE(!!s);
        *s->ptr += "[FC3]";
        unifex::set_value(std::move(r));
        if ((s->x % 2) == 0)
          return {FC2{{}, std::exchange(s, nullptr)}};
        return {FC4{{}, std::exchange(s, nullptr)}};
      }
      void unwind() noexcept {
        EXPECT_TRUE(!!s);
        *s->ptr += "[~FC3]";
        unifex::set_done(std::move(r));
        std::exchange(s, nullptr);
      }
      State* s;
      Receiver r;
    };
    template <typename Receiver>
    op<Receiver> connect(Receiver r) noexcept {
      EXPECT_TRUE(!!s);
      return {{}, s, r};
    }
    State* s;
  };

  struct FC1 : tail_sender_base {
    template <typename Receiver>
    struct op : tail_operation_state_base {
      FC2 start() noexcept {
        EXPECT_TRUE(!!s);
        *s->ptr += "[FC1]";
        s->x *= 5;
        unifex::set_value(std::move(r));
        return FC2{{}, std::exchange(s, nullptr)};
      }
      void unwind() noexcept {
        EXPECT_TRUE(!!s);
        *s->ptr += "[~FC1]";
        unifex::set_done(std::move(r));
        std::exchange(s, nullptr);
      }
      State* s;
      Receiver r;
    };
    template <typename Receiver>
    op<Receiver> connect(Receiver r) noexcept {
      EXPECT_TRUE(!!s);
      return {{}, s, r};
    }
    State* s;
  };
};
static_assert(tail_sender_to<State::FC1, null_tail_receiver>);
static_assert(tail_sender_to<State::FC2, null_tail_receiver>);
static_assert(tail_sender_to<State::FC3, null_tail_receiver>);
static_assert(tail_sender_to<State::FC4, null_tail_receiver>);

TEST(TailSender, Forks) {
  {
    std::string result;
    State s{3, &result};
    resume_tail_sender(State::FC1{{}, &s});
    std::cout << "result: " << result << "\n";
    EXPECT_EQ(result, "[FC1][FC2][FC3][FC2][FC3][FC4]");
  }

  {
    std::string result;
    State s{0, &result};
    resume_tail_sender(State::FC1{{}, &s});
    std::cout << "result: " << result << "\n";
    EXPECT_EQ(result, "[FC1][FC2][FC4]");
  }

  {
    std::string result;
    State s{0, &result};
    { scoped_tail_sender destroyer(State::FC1{{}, &s}); }
    std::cout << "result: " << result << "\n";
    EXPECT_EQ(result, "[~FC1]");
  }
}

TEST(TailSender, Interleave) {
  {
    std::string result;
    State s0{3, &result};
    State s1{0, &result};
    resume_tail_sender(
        resume_tail_senders_until_one_remaining(
            null_tail_receiver{},  //
            State::FC1{{}, &s0},   //
            State::FC1{{}, &s1}    //
            ),
        null_tail_receiver{});
    std::cout << "result: " << result << "\n";
    EXPECT_EQ(result, "[FC1][FC2][FC1][FC2][FC3][FC2][FC4][FC3][FC4]");
  }

  {
    std::string result;
    State s0{3, &result};
    State s1{0, &result};
    resume_tail_sender(
        resume_tail_senders_until_one_remaining(
            null_tail_receiver{},
            C4{{}, &result},
            State::FC1{{}, &s0},
            RC5{{}, &result, false},
            State::FC1{{}, &s1}),
        null_tail_receiver{});
    std::cout << "result: " << result << "\n";
    EXPECT_EQ(
        result,
        "[C4][==C3][C3][C2][FC1][FC2][RC5][RC4][RC3][==RC2][RC2][FC1][FC2]["
        "C1][FC3][FC2][RC1][RC3][FC4][FC3][FC4][==RC2]");
  }
}
