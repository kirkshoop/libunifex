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
#include <unifex/packaged_callable.hpp>
#include <unifex/sender_concepts.hpp>

#include <iostream>

#include <gtest/gtest.h>

using namespace unifex;
using namespace std::chrono;
using namespace std::chrono_literals;

struct send {
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = Variant<Tuple<>>;
  template <template <typename...> class Variant>
  using error_types = Variant<std::exception_ptr>;
  static inline constexpr bool sends_done = false;

  template <typename Receiver>
  struct op {
    explicit op(Receiver r) : r_(r) {}
    op(const op&) = delete;
    op(op&&) = delete;
    void start() noexcept { unifex::set_value(std::move(r_)); }
    Receiver r_;
  };
  template <typename Receiver>
  op<Receiver> connect(Receiver r) {
    return op<Receiver>{r};
  }
};
struct receive {
  void set_value() noexcept { *ptr += "[set_value]"; }
  void set_error(std::exception_ptr) noexcept { *ptr += "[set_error]"; }
  void set_done() noexcept { *ptr += "[set_done]"; }
  std::string* ptr;
};

TEST(TailSender, Smoke) {
  std::string result;
  auto connector = packaged_callable(unifex::connect, send{}, receive{&result});
  using connector_t = decltype(connector);

  EXPECT_TRUE(callable_package<connector_t>);

  if constexpr (callable_package<connector_t>) {
    using op_t = typename connector_t::value_type;

    std::optional<op_t> op{};

    op.emplace(connector);  // copy

    op.emplace(
        packaged_callable{unifex::connect, send{}, receive{&result}});  // move

    unifex::start(*op);
    op.reset();
    EXPECT_FALSE(op.has_value());
    EXPECT_EQ(result, "[set_value]");
    result.clear();

    std::optional<op_t> op2(connector);  // copy

    unifex::start(*op2);
    op2.reset();
    EXPECT_FALSE(op2.has_value());
    EXPECT_EQ(result, "[set_value]");
    result.clear();

    std::optional<op_t> op3(
        packaged_callable{unifex::connect, send{}, receive{&result}});  // move

    unifex::start(*op3);
    op3.reset();
    EXPECT_FALSE(op3.has_value());
    EXPECT_EQ(result, "[set_value]");
    result.clear();
  }

  {
    auto connector =
        packaged_callable{unifex::connect, send{}, receive{&result}};
    auto op{connector()};  // copy
    unifex::start(op);
    EXPECT_EQ(result, "[set_value]");
    result.clear();
  }

  {
    auto connector =
        packaged_callable{unifex::connect, send{}, receive{&result}};
    auto op{std::move(connector)()};  // move
    unifex::start(op);
    EXPECT_EQ(result, "[set_value]");
    result.clear();
  }

  {
    auto op{packaged_callable{
        unifex::connect, send{}, receive{&result}}()};  // move
    unifex::start(op);
    EXPECT_EQ(result, "[set_value]");
    result.clear();
  }
}
