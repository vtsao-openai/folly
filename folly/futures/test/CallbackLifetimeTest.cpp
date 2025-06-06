/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <thread>

#include <folly/futures/Future.h>
#include <folly/futures/test/TestExecutor.h>
#include <folly/portability/GTest.h>

using namespace folly;

namespace {

/***
 *  The basic premise is to check that the callback passed to then or thenError
 *  is destructed before wait returns on the resulting future.
 *
 *  The approach is to use callbacks where the destructor sleeps 500ms and then
 *  mutates a counter allocated on the caller stack. The caller checks the
 *  counter immediately after calling wait. Were the callback not destructed
 *  before wait returns, then we would very likely see an unchanged counter just
 *  after wait returns. But if, as we expect, the callback were destructed
 *  before wait returns, then we must be guaranteed to see a mutated counter
 *  just after wait returns.
 *
 *  Note that the failure condition is not strictly guaranteed under load. :(
 */
class CallbackLifetimeTest : public testing::Test {
 public:
  using CounterPtr = std::unique_ptr<size_t>;

  static bool kRaiseWillThrow() { return true; }
  static constexpr auto kDelay() { return std::chrono::milliseconds(500); }

  auto mkC() { return std::make_unique<size_t>(0); }
  auto mkCGuard(CounterPtr& ptr) {
    return makeGuard([&] {
      /* sleep override */ std::this_thread::sleep_for(kDelay());
      ++*ptr;
    });
  }

  static void raise(folly::Unit = folly::Unit{}) {
    if (kRaiseWillThrow()) { // to avoid marking [[noreturn]]
      throw std::runtime_error("raise");
    }
  }
  static Future<Unit> raiseFut() {
    raise();
    return makeFuture();
  }

  TestExecutor executor{2}; // need at least 2 threads for internal futures
};
} // namespace

TEST_F(CallbackLifetimeTest, thenReturnsValue) {
  auto c = mkC();
  via(&executor).thenValue([_ = mkCGuard(c)](auto&&) {}).wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenReturnsValueThrows) {
  auto c = mkC();
  via(&executor).thenValue([_ = mkCGuard(c)](auto&&) { raise(); }).wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenReturnsFuture) {
  auto c = mkC();
  via(&executor)
      .thenValue([_ = mkCGuard(c)](auto&&) { return makeFuture(); })
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenReturnsFutureThrows) {
  auto c = mkC();
  via(&executor)
      .thenValue([_ = mkCGuard(c)](auto&&) { return raiseFut(); })
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenErrorTakesExnReturnsValueMatch) {
  auto c = mkC();
  via(&executor)
      .thenValue(raise)
      .thenError(folly::tag_t<std::exception>{}, [_ = mkCGuard(c)](auto&&) {})
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenErrorTakesExnReturnsValueMatchThrows) {
  auto c = mkC();
  via(&executor)
      .thenValue(raise)
      .thenError(
          folly::tag_t<std::exception>{},
          [_ = mkCGuard(c)](auto&&) { raise(); })
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenErrorTakesExnReturnsValueWrong) {
  auto c = mkC();
  via(&executor)
      .thenValue(raise)
      .thenError(folly::tag_t<std::logic_error>{}, [_ = mkCGuard(c)](auto&&) {})
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenErrorTakesExnReturnsValueWrongThrows) {
  auto c = mkC();
  via(&executor)
      .thenValue(raise)
      .thenError(
          folly::tag_t<std::logic_error>{},
          [_ = mkCGuard(c)](auto&&) { raise(); })
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenErrorTakesExnReturnsFutureMatch) {
  auto c = mkC();
  via(&executor)
      .thenValue(raise)
      .thenError(
          folly::tag_t<std::exception>{},
          [_ = mkCGuard(c)](auto&&) { return makeFuture(); })
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenErrorTakesExnReturnsFutureMatchThrows) {
  auto c = mkC();
  via(&executor)
      .thenValue(raise)
      .thenError(
          folly::tag_t<std::exception>{},
          [_ = mkCGuard(c)](auto&&) { return raiseFut(); })
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenErrorTakesExnReturnsFutureWrong) {
  auto c = mkC();
  via(&executor)
      .thenValue(raise)
      .thenError(
          folly::tag_t<std::logic_error>{},
          [_ = mkCGuard(c)](auto&&) { return makeFuture(); })
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenErrorTakesExnReturnsFutureWrongThrows) {
  auto c = mkC();
  via(&executor)
      .thenValue(raise)
      .thenError(
          folly::tag_t<std::logic_error>{},
          [_ = mkCGuard(c)](auto&&) { return raiseFut(); })
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenErrorTakesWrapReturnsValue) {
  auto c = mkC();
  via(&executor)
      .thenValue(raise)
      .thenError([_ = mkCGuard(c)](exception_wrapper&&) {})
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenErrorTakesWrapReturnsValueThrows) {
  auto c = mkC();
  via(&executor)
      .thenValue(raise)
      .thenError([_ = mkCGuard(c)](exception_wrapper&&) { raise(); })
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenErrorTakesWrapReturnsFuture) {
  auto c = mkC();
  via(&executor)
      .thenValue(raise)
      .thenError([_ = mkCGuard(c)](exception_wrapper&&) {
        return makeFuture();
      })
      .wait();
  EXPECT_EQ(1, *c);
}

TEST_F(CallbackLifetimeTest, thenErrorTakesWrapReturnsFutureThrows) {
  auto c = mkC();
  via(&executor)
      .thenValue(raise)
      .thenError([_ = mkCGuard(c)](exception_wrapper&&) { return raiseFut(); })
      .wait();
  EXPECT_EQ(1, *c);
}
