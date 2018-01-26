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

// This include must come before any #ifndef check on Ceres compile options.
#include "ceres/internal/port.h"

#ifdef CERES_USE_CXX11_THREADS

#include "ceres/thread_pool.h"

#include <cmath>

namespace ceres {
namespace internal {
namespace {
// Constrain the total number of threads to the amount the hardware can
// support.
int ConstrainNumThreads(int requested_num_threads) {
  const int num_hardware_threads = std::thread::hardware_concurrency();
  int num_threads_create;
  // hardware_concurrency() can return 0 if the value is not well defined or not
  // computable.
  if (num_hardware_threads == 0) {
    num_threads_create = requested_num_threads;
  } else {
    num_threads_create = std::min(requested_num_threads, num_hardware_threads);
  }
  return num_threads_create;
}
}  // namespace

ThreadPool::ThreadPool() { }

ThreadPool::ThreadPool(int num_threads) {
  Resize(num_threads);
}

ThreadPool::~ThreadPool() {
  // Signal the thread workers to stop and wait for them to finish any started
  // work.
  Stop();
  for (std::thread& thread : thread_pool_) {
    thread.join();
  }
}

void ThreadPool::Resize(int num_threads) {
  std::unique_lock<std::mutex> lock(thread_pool_mutex_);

  const int num_current_threads = thread_pool_.size();
  if (num_current_threads >= num_threads) {
    return;
  }

  const int num_threads_create = ConstrainNumThreads(num_threads);

  for (int i = 0; i < num_threads_create - num_current_threads; ++i) {
    thread_pool_.push_back(std::thread(&ThreadPool::ThreadMainLoop, this));
  }
}

void ThreadPool::AddTask(const std::function<void()>& func) {
  task_queue_.Push(func);
}

int ThreadPool::Size() {
  std::unique_lock<std::mutex> lock(thread_pool_mutex_);
  return thread_pool_.size();
}

void ThreadPool::ThreadMainLoop() {
  std::function<void()> task;
  while (task_queue_.Pop(&task)) {
    task();
  }
}

void ThreadPool::Stop() { task_queue_.Stop(); }

}  // namespace internal
}  // namespace ceres

#endif // CERES_USE_CXX11_THREADS
