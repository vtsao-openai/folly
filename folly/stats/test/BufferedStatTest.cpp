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

#include <folly/stats/detail/BufferedStat.h>

#include <folly/Range.h>
#include <folly/portability/GTest.h>

using namespace folly;
using namespace folly::detail;

namespace {

const size_t kDigestSize = 100;

struct MockClock {
 public:
  using duration = std::chrono::steady_clock::duration;
  using time_point = std::chrono::steady_clock::time_point;

  static time_point now() { return Now; }

  static time_point Now;
};

class SimpleDigest {
 public:
  explicit SimpleDigest(size_t sz) { EXPECT_EQ(kDigestSize, sz); }

  SimpleDigest merge(Range<const double*> r) const {
    SimpleDigest digest(100);

    digest.values_ = values_;
    for (auto it = r.begin(); it != r.end(); ++it) {
      digest.values_.push_back(*it);
    }
    return digest;
  }

  static SimpleDigest merge(Range<const SimpleDigest**> r) {
    SimpleDigest digest(100);
    for (auto it = r.begin(); it != r.end(); ++it) {
      const auto& values = (*it)->values_;
      digest.values_.insert(digest.values_.end(), values.begin(), values.end());
    }
    return digest;
  }

  static SimpleDigest merge(Range<const SimpleDigest*> r) {
    SimpleDigest digest(100);
    for (auto it = r.begin(); it != r.end(); ++it) {
      const auto& values = it->values_;
      digest.values_.insert(digest.values_.end(), values.begin(), values.end());
    }
    return digest;
  }

  static SimpleDigest merge(const SimpleDigest& d1, const SimpleDigest& d2) {
    std::array<const SimpleDigest*, 2> ds = {&d1, &d2};
    return merge(range(ds));
  }

  std::vector<double> getValues() const { return values_; }

  bool empty() const { return values_.empty(); }

 private:
  std::vector<double> values_;
};

MockClock::time_point MockClock::Now = MockClock::time_point{};

} // namespace

class BufferedDigestTest : public ::testing::Test {
 protected:
  std::unique_ptr<BufferedDigest<SimpleDigest, MockClock>> bd;
  const size_t nBuckets = 60;
  const size_t bufferSize = 1000;
  const std::chrono::milliseconds bufferDuration{1000};

  void SetUp() override {
    MockClock::Now = MockClock::time_point{};
    bd = std::make_unique<BufferedDigest<SimpleDigest, MockClock>>(
        bufferDuration, bufferSize, kDigestSize);
  }
};

TEST_F(BufferedDigestTest, Buffering) {
  bd->append(0);
  bd->append(1);
  bd->append(2);

  auto digest = bd->get();
  EXPECT_TRUE(digest.empty());
}

TEST_F(BufferedDigestTest, PartiallyPassedExpiry) {
  bd->append(0);
  bd->append(1);
  bd->append(2);

  MockClock::Now += bufferDuration / 10;

  auto digest = bd->get();

  auto values = digest.getValues();
  EXPECT_EQ(0, values[0]);
  EXPECT_EQ(1, values[1]);
  EXPECT_EQ(2, values[2]);
}

TEST_F(BufferedDigestTest, ForceUpdate) {
  bd->append(0);
  bd->append(1);
  bd->append(2);

  // empty since we haven't passed expiry
  auto digest = bd->get();
  EXPECT_TRUE(digest.empty());

  // force update
  bd->flush();
  digest = bd->get();
  auto values = digest.getValues();
  EXPECT_EQ(0, values[0]);
  EXPECT_EQ(1, values[1]);
  EXPECT_EQ(2, values[2]);

  // append 3 and do a normal get; only the previously
  // flushed values should show up and not 3 since we
  // haven't passed expiry
  bd->append(3);
  digest = bd->get();
  values = digest.getValues();
  EXPECT_EQ(0, values[0]);
  EXPECT_EQ(1, values[1]);
  EXPECT_EQ(2, values[2]);

  // pass expiry; 3 should now be visible
  MockClock::Now += bufferDuration;
  digest = bd->get();
  values = digest.getValues();
  EXPECT_EQ(0, values[0]);
  EXPECT_EQ(1, values[1]);
  EXPECT_EQ(2, values[2]);
  EXPECT_EQ(3, values[3]);
}

class BufferedSlidingWindowTest : public ::testing::Test {
 protected:
  std::unique_ptr<BufferedSlidingWindow<SimpleDigest, MockClock>> bsw;
  const size_t nBuckets = 60;
  const size_t bufferSize = 1000;
  const std::chrono::milliseconds windowDuration{1000};

  void SetUp() override {
    MockClock::Now = MockClock::time_point{};
    bsw = std::make_unique<BufferedSlidingWindow<SimpleDigest, MockClock>>(
        nBuckets, windowDuration, bufferSize, kDigestSize);
  }
};

TEST_F(BufferedSlidingWindowTest, Buffering) {
  bsw->append(0);
  bsw->append(1);
  bsw->append(2);

  auto digests = bsw->get();
  EXPECT_EQ(0, digests.size());
}

TEST_F(BufferedSlidingWindowTest, PartiallyPassedExpiry) {
  bsw->append(0);
  bsw->append(1);
  bsw->append(2);

  MockClock::Now += windowDuration / 10;

  auto digests = bsw->get();

  EXPECT_EQ(1, digests.size());
  EXPECT_EQ(3, digests[0].getValues().size());

  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(double(i), digests[0].getValues()[i]);
  }
}

TEST_F(BufferedSlidingWindowTest, ForceUpdate) {
  bsw->append(0);
  bsw->append(1);
  bsw->append(2);

  // empty since we haven't passed expiry
  auto digests = bsw->get();
  EXPECT_EQ(0, digests.size());

  // flush
  bsw->flush();
  digests = bsw->get();
  EXPECT_EQ(1, digests.size());
  EXPECT_EQ(3, digests[0].getValues().size());
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(double(i), digests[0].getValues()[i]);
  }

  // append 3 and flush again; 3 will be merged with
  // current window
  bsw->append(3);
  bsw->flush();
  digests = bsw->get();
  EXPECT_EQ(1, digests.size());
  EXPECT_EQ(4, digests[0].getValues().size());
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(double(i), digests[0].getValues()[i]);
  }

  // append 4 and do a regular get. previous values
  // show up but not 4
  bsw->append(4);
  digests = bsw->get();
  EXPECT_EQ(1, digests.size());
  EXPECT_EQ(4, digests[0].getValues().size());
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(double(i), digests[0].getValues()[i]);
  }

  // pass expiry
  MockClock::Now += windowDuration;
  digests = bsw->get();
  EXPECT_EQ(2, digests.size());

  EXPECT_EQ(1, digests[0].getValues().size());
  EXPECT_EQ(4, digests[0].getValues().front());

  EXPECT_EQ(4, digests[1].getValues().size());
  for (size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(double(i), digests[1].getValues()[i]);
  }
}

TEST_F(BufferedSlidingWindowTest, BufferingAfterSlide) {
  MockClock::Now += std::chrono::milliseconds{1};

  bsw->append(1);

  auto digests = bsw->get();
  EXPECT_EQ(0, digests.size());
}

TEST_F(BufferedSlidingWindowTest, TwoSlides) {
  bsw->append(0);

  MockClock::Now += windowDuration;

  bsw->append(1);

  MockClock::Now += windowDuration;

  auto digests = bsw->get();

  EXPECT_EQ(2, digests.size());
  EXPECT_EQ(1, digests[0].getValues().size());
  EXPECT_EQ(1, digests[0].getValues()[0]);
  EXPECT_EQ(1, digests[1].getValues().size());
  EXPECT_EQ(0, digests[1].getValues()[0]);
}

TEST_F(BufferedSlidingWindowTest, MultiWindowDurationSlide) {
  bsw->append(0);

  MockClock::Now += windowDuration * 2;

  auto digests = bsw->get();
  EXPECT_EQ(1, digests.size());
}

TEST_F(BufferedSlidingWindowTest, SlidePastWindow) {
  bsw->append(0);

  MockClock::Now += windowDuration * (nBuckets + 1);

  auto digests = bsw->get();

  EXPECT_EQ(0, digests.size());
}

TEST(BufferedMultiSlidingWindow, Equivalence) {
  // Verify that BufferedMultiSlidingWindow returns exactly the same digests as
  // BufferedDigest + BufferedSlidingWindow.

  using BSW = BufferedSlidingWindow<SimpleDigest, MockClock>;
  using BMSW = BufferedMultiSlidingWindow<SimpleDigest, MockClock>;

  MockClock::Now = MockClock::time_point{};
  const size_t bufferSize = 1000;
  const size_t kNumValues = 500;
  std::vector<BMSW::WindowDef> defs = {
      {std::chrono::seconds{1}, 5},
      {std::chrono::seconds{2}, 5},
      {std::chrono::seconds{3}, 5},
  };

  BMSW bmsw{defs, bufferSize, kDigestSize};

  // Reference buffered stats.
  BufferedDigest<SimpleDigest, MockClock> allTime{
      std::chrono::seconds{1}, bufferSize, kDigestSize};
  std::vector<std::unique_ptr<BSW>> bsws;
  bsws.reserve(defs.size());
  for (const auto& def : defs) {
    bsws.push_back(
        std::make_unique<BSW>(def.second, def.first, bufferSize, kDigestSize));
  }

  const auto digestsValues = [](const auto& ds) {
    std::vector<std::vector<double>> vals;
    vals.reserve(ds.size());
    for (const auto& d : ds) {
      vals.push_back(d.getValues());
    }
    return vals;
  };

  const auto validate = [&] {
    auto digests = bmsw.get();
    EXPECT_EQ(digests.allTime.getValues(), allTime.get().getValues());
    for (size_t w = 0; w < bsws.size(); ++w) {
      EXPECT_EQ(
          digestsValues(digests.windows[w]), digestsValues(bsws[w]->get()));
    }
  };

  for (size_t i = 0; i < kNumValues; ++i) {
    // Periodically check equivalence.
    if (i % 10 == 0) {
      validate();
    }
    bmsw.append(i);
    allTime.append(i);
    for (auto& bsw : bsws) {
      bsw->append(i);
    }
    // Advance the clock by a prime so that buckets are not periodic.
    MockClock::Now += std::chrono::milliseconds{137};
    // Add a couple of gaps that leave some buckets empty.
    if (i % 200 == 0) {
      MockClock::Now += std::chrono::seconds{(i / 200 + 1) * 5};
    }
  }

  // Digests should be equivalent after a flush as well.
  bmsw.flush();
  allTime.flush();
  for (auto& bsw : bsws) {
    bsw->flush();
  }
  validate();

  // Verify that the test is not accidentally trivial and the windows slid at
  // least once.
  auto digests = bmsw.get();
  EXPECT_EQ(digests.allTime.getValues().size(), kNumValues);
  for (const auto& w : digests.windows) {
    EXPECT_LT(SimpleDigest::merge(w).getValues().size(), kNumValues);
  }
}
