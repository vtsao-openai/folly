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

/*
 *  This file serves as a helper for bridging folly::future and python
 *  asyncio.future.
 */

#pragma once

#include <folly/Executor.h>
#include <folly/futures/Future.h>
#include <folly/python/AsyncioExecutor.h>
#include <folly/python/Weak.h>
#include <folly/python/executor.h>

namespace folly {
namespace python {

template <typename T>
void bridgeFuture(
    folly::Executor* executor,
    folly::Future<T>&& futureFrom,
    folly::Function<void(folly::Try<T>&&, PyObject*)> callback,
    PyObject* userData) {
  // We are handing over a pointer to a python object to c++ and need
  // to make sure it isn't removed by python in that time.
  Py_IncRef(userData);
  auto guard = folly::makeGuard([=] { Py_DecRef(userData); });
  // Handle the lambdas for cython
  // run callback from our Q
  futureFrom.via(executor).then(
      [callback = std::move(callback), userData, guard = std::move(guard)](
          folly::Try<T>&& res) mutable {
        // This will run from inside the gil, called by the asyncio add_reader
        callback(std::move(res), userData);
        // guard goes out of scope here, and its stored function is called
      });
}

template <typename T>
void bridgeFuture(
    folly::Future<T>&& futureFrom,
    folly::Function<void(folly::Try<T>&&, PyObject*)> callback,
    PyObject* userData) {
  bridgeFuture(
      getExecutor(), std::move(futureFrom), std::move(callback), userData);
}

template <typename T>
void bridgeSemiFuture(
    folly::Executor* executor,
    folly::SemiFuture<T>&& semiFutureFrom,
    folly::Function<void(folly::Try<T>&&, PyObject*)> callback,
    PyObject* userData) {
  folly::Future<T> futureFrom = std::move(semiFutureFrom).via(executor);
  bridgeFuture(executor, std::move(futureFrom), std::move(callback), userData);
}

template <typename T>
void bridgeSemiFuture(
    folly::SemiFuture<T>&& semiFutureFrom,
    folly::Function<void(folly::Try<T>&&, PyObject*)> callback,
    PyObject* userData) {
  bridgeSemiFuture(
      getExecutor(), std::move(semiFutureFrom), std::move(callback), userData);
}

} // namespace python
} // namespace folly
