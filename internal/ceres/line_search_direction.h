// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2022 Google Inc. All rights reserved.
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
// Author: sameeragarwal@google.com (Sameer Agarwal)

#ifndef CERES_INTERNAL_LINE_SEARCH_DIRECTION_H_
#define CERES_INTERNAL_LINE_SEARCH_DIRECTION_H_

#include <memory>

#include "ceres/internal/eigen.h"
#include "ceres/internal/export.h"
#include "ceres/line_search_minimizer.h"
#include "ceres/types.h"

namespace ceres::internal {

class CERES_NO_EXPORT LineSearchDirection {
 public:
  struct Options {
    int num_parameters{0};
    LineSearchDirectionType type{LBFGS};
    NonlinearConjugateGradientType nonlinear_conjugate_gradient_type{
        FLETCHER_REEVES};
    double function_tolerance{1e-12};
    int max_lbfgs_rank{20};
    bool use_approximate_eigenvalue_bfgs_scaling{true};
  };

  static std::unique_ptr<LineSearchDirection> Create(const Options& options);

  virtual ~LineSearchDirection();
  virtual bool NextDirection(const LineSearchMinimizer::State& previous,
                             const LineSearchMinimizer::State& current,
                             Vector* search_direction) = 0;
};

}  // namespace ceres

#endif  // CERES_INTERNAL_LINE_SEARCH_DIRECTION_H_
