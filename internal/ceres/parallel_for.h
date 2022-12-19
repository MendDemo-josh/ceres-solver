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
// Authors: vitus@google.com (Michael Vitus),
//          dmitriy.korchemkin@gmail.com (Dmitriy Korchemkin)

#ifndef CERES_INTERNAL_PARALLEL_FOR_H_
#define CERES_INTERNAL_PARALLEL_FOR_H_

#include <mutex>
#include <vector>

#include "ceres/context_impl.h"
#include "ceres/internal/disable_warnings.h"
#include "ceres/internal/eigen.h"
#include "ceres/internal/export.h"
#include "ceres/parallel_for_invoke_impl.h"
#include "ceres/parallel_for_partition.h"
#include "glog/logging.h"

namespace ceres::internal {

// Use a dummy mutex if num_threads = 1.
inline decltype(auto) MakeConditionalLock(const int num_threads,
                                          std::mutex& m) {
  return (num_threads == 1) ? std::unique_lock<std::mutex>{}
                            : std::unique_lock<std::mutex>{m};
}

// Returns the maximum supported number of threads
CERES_NO_EXPORT int MaxNumThreadsAvailable();

// Execute the function for every element in the range [start, end) with at most
// num_threads. It will execute all the work on the calling thread if
// num_threads or (end - start) is equal to 1.
// Depending on function signature, it will be supplied with either loop index
// or a range of loop indicies; function can also be supplied with thread_id.
// The following function signatures are supported:
//  - Functions accepting a single loop index:
//     - [](int index) { ... }
//     - [](int thread_id, int index) { ... }
//  - Functions accepting a range of loop index:
//     - [](std::tuple<int, int> index) { ... }
//     - [](int thread_id, std::tuple<int, int> index) { ... }
//
// When distributing workload between threads, it is assumed that each loop
// iteration takes approximately equal time to complete.
template <typename F>
void ParallelFor(ContextImpl* context,
                 int start,
                 int end,
                 int num_threads,
                 const F& function) {
  CHECK_GT(num_threads, 0);
  if (start >= end) {
    return;
  }

  if (num_threads == 1 || end - start == 1) {
    InvokeOnSegment<F>(0, std::make_tuple(start, end), function);
    return;
  }

  CHECK(context != nullptr);
  ParallelInvoke<F>(context, start, end, num_threads, function);
}

// Execute function for every element in the range [start, end) with at most
// num_threads, using user-provided partitions array.
// When distributing workload between threads, it is assumed that each segment
// bounded by adjacent elements of partitions array takes approximately equal
// time to process.
template <typename F>
void ParallelFor(ContextImpl* context,
                 int start,
                 int end,
                 int num_threads,
                 const F& function,
                 const std::vector<int>& partitions) {
  CHECK_GT(num_threads, 0);
  if (start >= end) {
    return;
  }
  CHECK_EQ(partitions.front(), start);
  CHECK_EQ(partitions.back(), end);
  if (num_threads == 1 || end - start <= num_threads) {
    ParallelFor(context, start, end, num_threads, function);
    return;
  }
  CHECK_GT(partitions.size(), 1);
  const int num_partitions = partitions.size() - 1;
  ParallelFor(context,
              0,
              num_partitions,
              num_threads,
              [&function, &partitions](int thread_id,
                                       std::tuple<int, int> partition_ids) {
                const auto [partition_start, partition_end] = partition_ids;
                const int range_start = partitions[partition_start];
                const int range_end = partitions[partition_end];
                const auto range = std::make_tuple(range_start, range_end);
                InvokeOnSegment<F>(thread_id, range, function);
              });
}

// Execute function for every element in the range [start, end) with at most
// num_threads, taking into account user-provided integer cumulative costs of
// iterations. Cumulative costs of iteration for indices in range [0, end) are
// stored in objects from cumulative_cost_data. User-provided
// cumulative_cost_fun returns non-decreasing integer values corresponding to
// inclusive cumulative cost of loop iterations, provided with a reference to
// user-defined object. Only indices from [start, end) will be referenced. This
// routine assumes that cumulative_cost_fun is non-decreasing (in other words,
// all costs are non-negative);
// When distributing workload between threads, input range of loop indices will
// be partitioned into disjoint contiguous intervals, with the maximal cost
// being minimized.
// For example, with iteration costs of [1, 1, 5, 3, 1, 4] cumulative_cost_fun
// should return [1, 2, 7, 10, 11, 15], and with num_threads = 4 this range
// will be split into segments [0, 2) [2, 3) [3, 5) [5, 6) with costs
// [2, 5, 4, 4].
template <typename F, typename CumulativeCostData, typename CumulativeCostFun>
void ParallelFor(ContextImpl* context,
                 int start,
                 int end,
                 int num_threads,
                 const F& function,
                 const CumulativeCostData* cumulative_cost_data,
                 const CumulativeCostFun& cumulative_cost_fun) {
  CHECK_GT(num_threads, 0);
  if (start >= end) {
    return;
  }
  if (num_threads == 1 || end - start <= num_threads) {
    ParallelFor(context, start, end, num_threads, function);
    return;
  }
  // Creating several partitions allows us to tolerate imperfections of
  // partitioning and user-supplied iteration costs up to a certain extent
  const int kNumPartitionsPerThread = 4;
  const int kMaxPartitions = num_threads * kNumPartitionsPerThread;
  const std::vector<int> partitions = ComputePartition(
      start, end, kMaxPartitions, cumulative_cost_data, cumulative_cost_fun);
  CHECK_GT(partitions.size(), 1);
  ParallelFor(context, start, end, num_threads, function, partitions);
}

// Evaluate vector expression in parallel
// Assuming LhsExpression and RhsExpression are some sort of
// column-vector expression, assignment lhs = rhs
// is eavluated over a set of contiguous blocks in parallel.
// This is expected to work well in the case of vector-based
// expressions (since they typically do not result into
// temporaries).
// This method expects lhs to be size-compatible with rhs
template <typename LhsExpression, typename RhsExpression>
void ParallelAssign(ContextImpl* context,
                    int num_threads,
                    LhsExpression& lhs,
                    const RhsExpression& rhs) {
  static_assert(LhsExpression::ColsAtCompileTime == 1);
  static_assert(RhsExpression::ColsAtCompileTime == 1);
  CHECK_EQ(lhs.rows(), rhs.rows());
  const int num_rows = lhs.rows();
  ParallelFor(context,
              0,
              num_rows,
              num_threads,
              [&lhs, &rhs](const std::tuple<int, int>& range) {
                auto [start, end] = range;
                lhs.segment(start, end - start) =
                    rhs.segment(start, end - start);
              });
}

// Set vector to zero using num_threads
template <typename VectorType>
void ParallelSetZero(ContextImpl* context,
                     int num_threads,
                     VectorType& vector) {
  ParallelSetZero(context, num_threads, vector.data(), vector.rows());
}
void ParallelSetZero(ContextImpl* context,
                     int num_threads,
                     double* values,
                     int num_values);

}  // namespace ceres::internal

#endif  // CERES_INTERNAL_PARALLEL_FOR_H_
