// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2014 Google Inc. All rights reserved.
// http://code.google.com/p/ceres-solver/
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
// Author: sameragarwal@google.com (Sameer Agarwal)

#ifndef CERES_INTERNAL_PREPROCESSOR_H_
#define CERES_INTERNAL_PREPROCESSOR_H_

#include "ceres/coordinate_descent_minimizer.h"
#include "ceres/evaluator.h"
#include "ceres/internal/eigen.h"
#include "ceres/internal/port.h"
#include "ceres/internal/scoped_ptr.h"
#include "ceres/iteration_callback.h"
#include "ceres/linear_solver.h"
#include "ceres/minimizer.h"
#include "ceres/problem_impl.h"
#include "ceres/program.h"
#include "ceres/solver.h"

namespace ceres {
namespace internal {

struct PreprocessedProblem {
  PreprocessedProblem()
      : fixed_cost(0.0) {
  }

  string error;
  Solver::Options options;
  Minimizer::Options minimizer_options;

  ProblemImpl* problem;
  scoped_ptr<ProblemImpl> gradient_checking_problem;
  scoped_ptr<Program> reduced_program;
  scoped_ptr<LinearSolver> linear_solver;
  scoped_ptr<IterationCallback> logging_callback;
  scoped_ptr<IterationCallback> state_updating_callback;

  shared_ptr<Evaluator> evaluator;
  shared_ptr<CoordinateDescentMinimizer> inner_iteration_minimizer;

  vector<double*> removed_parameter_blocks;
  Vector reduced_parameters;
  double fixed_cost;
};

class Preprocessor {
public:
  virtual ~Preprocessor();
  virtual bool Preprocess(const Solver::Options& options,
                          ProblemImpl* problem,
                          PreprocessedProblem* preprocessed_problem) = 0;
};

// Common functions used by various preprocessors.

ProblemImpl* CreateGradientCheckingProblem(const Solver::Options& options,
                                           ProblemImpl* problem);
void ChangeNumThreadsIfNeeded(Solver::Options* options);
void SetupCommonMinimizerOptions(PreprocessedProblem* preprocessed_problem);

}  // namespace internal
}  // namespace ceres

#endif  // CERES_INTERNAL_PREPROCESSOR_H_
