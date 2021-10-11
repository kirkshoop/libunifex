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
#include <unifex/tail_callable_concepts.hpp>

#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

// Straight line - with conditional

struct C1 {
    void invoke() const noexcept {*ptr += "[C1]";}
    void destroy() const noexcept {*ptr += "[~C1]";}
    std::string* ptr;
};

struct C2 {
    C1 invoke() const noexcept { *ptr += "[C2]"; return {ptr}; }
    void destroy() const noexcept {*ptr += "[~C2]";}
    std::string* ptr;
};

struct C3 {
    explicit operator bool() const noexcept { *ptr += "[==C3]"; return ptr != nullptr; }
    C2 invoke() const noexcept { *ptr += "[C3]"; return {ptr}; }
    void destroy() const noexcept  {*ptr += "[~C3]";}
    std::string* ptr;
};

struct C4 {
    C3 invoke() const noexcept { *ptr += "[C4]"; return {ptr}; }
    void destroy() const noexcept {*ptr += "[~C4]";}
    std::string* ptr;
};

TEST(TailCallable, Smoke) {
  {
  std::string result;
  resume_tail_callable(C4{&result});
  std::cout << "result: " << result << "\n";
  EXPECT_EQ(result, "[C4][==C3][C3][C2][C1]");
  }

  {
  std::string result;
  {
  scoped_tail_callable destroyer(C4{&result});
  }
  std::cout << "result: " << result << "\n";
  EXPECT_EQ(result, "[~C4]");
  }
}

// Recursive - unrolled to loops
struct RC3;

struct RC1 {
    RC3 invoke() const noexcept;
    void destroy() const noexcept { *ptr += "[~RC1]"; }
    std::string* ptr;
    bool done;
};

struct RC2 {
    explicit operator bool() const noexcept { *ptr += "[==RC2]"; return !done; }
    RC1 invoke() const noexcept;
    void destroy() const noexcept { *ptr += "[~RC2]"; }
    std::string* ptr;
    bool done;
};

struct RC3 {
    RC2 invoke() const noexcept { *ptr += "[RC3]"; return {ptr, done}; }
    void destroy() const noexcept { *ptr += "[~RC3]"; }
    std::string* ptr;
    bool done;
};

RC3 RC1::invoke() const noexcept { *ptr += "[RC1]"; return {ptr, done}; }
RC1 RC2::invoke() const noexcept { *ptr += "[RC2]"; return {ptr, true}; }

struct RC4 {
    RC3 invoke() const noexcept { *ptr += "[RC4]"; return {ptr, done}; }
    void destroy() const noexcept { *ptr += "[~RC4]"; }
    std::string* ptr;
    bool done;
};

struct RC5 {
    RC4 invoke() const noexcept { *ptr += "[RC5]"; return {ptr, done}; }
    void destroy() const noexcept { *ptr += "[~RC5]"; }
    std::string* ptr;
    bool done;
};

TEST(TailCallable, Recursive) {
  {
  std::string result;
  resume_tail_callable(RC5{&result, false});
  std::cout << "result: " << result << "\n";
  EXPECT_EQ(result, "[RC5][RC4][RC3][==RC2][RC2][RC1][RC3][==RC2]");
  }

  {
  std::string result;
  {
  scoped_tail_callable destroyer(RC5{&result, false});
  }
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

    struct FC4 {
        void invoke() const noexcept { *s->ptr += "[FC4]"; s->x = -s->x; }
        void destroy() const noexcept { *s->ptr += "[~FC4]"; }
        State* s;
    };

    struct FC2 {
        variant_tail_callable<FC3, FC4> invoke() const noexcept {
            *s->ptr += "[FC2]";
            s->x--;
            if (s->x >= 0) return FC3{s};
            return FC4{s};
        }
        void destroy() const noexcept { *s->ptr += "[~FC2]"; }
        State* s;
    };

    struct FC3 {
        variant_tail_callable<FC2, FC4> invoke() const noexcept {
            *s->ptr += "[FC3]";
            if ((s->x % 2) == 0) return FC2{s};
            return FC4{s};
        }
        void destroy() const noexcept { *s->ptr += "[~FC3]"; }
        State* s;
    };

    struct FC1 {
        FC2 invoke() const noexcept { *s->ptr += "[FC1]"; s->x *= 5; return FC2{s}; }
        void destroy() const noexcept { *s->ptr += "[~FC1]"; }
        State* s;
    };
};

TEST(TailCallable, Forks) {
  {
  std::string result;
  State s{3, &result};
  resume_tail_callable(State::FC1{&s});
  std::cout << "result: " << result << "\n";
  EXPECT_EQ(result, "[FC1][FC2][FC3][FC2][FC3][FC4]");
  }

  {
  std::string result;
  State s{0, &result};
  resume_tail_callable(State::FC1{&s});
  std::cout << "result: " << result << "\n";
  EXPECT_EQ(result, "[FC1][FC2][FC4]");
  }

  {
  std::string result;
  State s{0, &result};
  {
  scoped_tail_callable destroyer(State::FC1{&s});
  }
  std::cout << "result: " << result << "\n";
  EXPECT_EQ(result, "[~FC1]");
  }
}

TEST(TailCallable, Interleave) {
  {
  std::string result;
  State s0{3, &result};
  State s1{0, &result};
  resume_tail_callables_until_one_remaining(State::FC1{&s0}, State::FC1{&s1});
  std::cout << "result: " << result << "\n";
  EXPECT_EQ(result, "[FC1][FC2][FC3][FC1][FC2][FC4][FC2][FC3]");
  }

  {
  std::string result;
  State s0{3, &result};
  State s1{0, &result};
  resume_tail_callables_until_one_remaining(C4{&result}, State::FC1{&s0}, RC5{&result, false}, State::FC1{&s1});
  std::cout << "result: " << result << "\n";
  EXPECT_EQ(result, "[C4][==C3][C3][C2][C1][FC1][FC2][FC3][RC5][RC4][RC3][FC1][FC2][FC4][FC2][FC3][==RC2][RC2][RC1][RC3][FC4][==RC2]");
  }
}
