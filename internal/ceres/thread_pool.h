// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2018 Google Inc. All rights reserved.
// http://ceres-solver.org/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name of Google Inc. nor the names of its contributors may be
//   used to endorse or promote products derived from this software without
//   specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Author: vitus@google.com (Michael Vitus)

#ifndef CERES_INTERNAL_THREAD_POOL_H_
#define CERES_INTERNAL_THREAD_POOL_H_

#include <mutex>
#include <thread>
#include <vector>

#include "ceres/concurrent_queue.h"

namespace ceres {
namespace internal {

// A thread pool with a fixed number of workers and an unbounded task
// queue.
class ThreadPool {
 public:
  // Default constructor with no active threads.  We allow instantiating a
  // thread pool with no threads to support the use case of single threaded
  // Ceres where everything will be executed on the main thread. For single
  // threaded execution this has two benefits: avoid any overhead as threads
  // are expensive to create, and no unused threads shown in the debugger.
  ThreadPool();

  // Instantiates a thread pool with min(num_hardware_threads, num_threads)
  // number of threads.
  explicit ThreadPool(int num_threads);

  ~ThreadPool();

  // Resize the thread pool if it is currently less than the requested number of
  // threads.  The thread pool will be resized to min(num_hardware_threads,
  // num_threads) number of threads.  Resize does not support reducing the
  // thread pool size.  The thread pool is reused within Ceres with different
  // number of threads, and we need to ensure we can support the largest number
  // of threads requested.
  void Resize(int num_threads);

  // Add a task to the queue and wake up a blocked thread.  If the thread pool
  // size is greater than zero, then the task will be executed by a currently
  // idle thread or when a thread becomes available.  If the thread pool has no
  // threads, then the task will never be executed and the user should use
  // Resize() to create a non-empty thread pool.
  void AddTask(const std::function<void()>& func);

  // Returns the current size of the thread pool.
  int Size();

 private:
  // Main loop for the threads which blocks on the task queue until work becomes
  // available.
  void ThreadMainLoop();

  // Signal all the threads to stop.
  void Stop();

  ConcurrentQueue<std::function<void()>> task_queue_;
  std::vector<std::thread> thread_pool_;
  std::mutex thread_pool_mutex_;
};

}  // namespace internal
}  // namespace ceres

#endif  // CERES_INTERNAL_THREAD_POOL_H_
