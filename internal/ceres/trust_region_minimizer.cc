// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2016 Google Inc. All rights reserved.
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

#include "ceres/trust_region_minimizer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "Eigen/Core"
#include "ceres/array_utils.h"
#include "ceres/coordinate_descent_minimizer.h"
#include "ceres/evaluator.h"
#include "ceres/file.h"
#include "ceres/line_search.h"
#include "ceres/stringprintf.h"
#include "ceres/types.h"
#include "ceres/wall_time.h"
#include "glog/logging.h"

#define RETURN_IF_ERROR_AND_LOG(expr)                                   \
  do {                                                                  \
    if (!(expr)) {                                                      \
      LOG(ERROR) << "Terminating: " << solver_summary_->message;        \
      return;                                                           \
    }                                                                   \
  } while (0)

namespace ceres {
namespace internal {

void TrustRegionMinimizer::Minimize(const Minimizer::Options& options,
                                    double* parameters,
                                    Solver::Summary* solver_summary) {
  start_time_ = WallTimeInSeconds();
  iteration_start_time_ =  start_time_;
  Init(options, parameters, solver_summary);
  RETURN_IF_ERROR_AND_LOG(IterationZero());

  VectorRef(parameters, num_parameters_) = x_;

  if (options_.use_nonmonotonic_steps) {
    step_evaluator_.reset(
        new TointNonMonotonicStepEvaluator(
            cost_,
            options_.max_consecutive_nonmonotonic_steps,
            is_not_silent_));
  } else {
    step_evaluator_.reset(new MonotonicStepEvaluator(cost_));
  }

  double minimum_cost = cost_;
  while (FinalizeIterationAndCheckIfMinimizerCanContinue()) {
    iteration_start_time_ = WallTimeInSeconds();
    iteration_summary_ = IterationSummary();
    iteration_summary_.iteration =
        solver_summary->iterations.back().iteration + 1;

    RETURN_IF_ERROR_AND_LOG(ComputeTrustRegionStep());
    if (!iteration_summary_.step_is_valid) {
      RETURN_IF_ERROR_AND_LOG(HandleInvalidStep());
      continue;
    }

    num_consecutive_invalid_steps_ = 0;
    // Undo the Jacobian column scaling.
    delta_ = (trust_region_step_.array() * scale_.array()).matrix();

    if (options_.is_constrained) {
      DoLineSearch(x_, gradient_, cost_, &delta_);
    }

    double new_cost = std::numeric_limits<double>::max();
    if (evaluator_->Plus(x_.data(), delta_.data(), x_plus_delta_.data())) {
      if (!evaluator_->Evaluate(x_plus_delta_.data(),
                                &new_cost,
                                NULL,
                                NULL,
                                NULL)) {
        LOG_IF(WARNING, is_not_silent_)
            << "Step failed to evaluate. "
            << "Treating it as a step with infinite cost";
        new_cost = std::numeric_limits<double>::max();
      }
    } else {
      LOG_IF(WARNING, is_not_silent_)
          << "x_plus_delta = Plus(x, delta) failed. "
          << "Treating it as a step with infinite cost";
    }

    if (new_cost < std::numeric_limits<double>::max() &&
        inner_iterations_are_enabled_) {
      const double inner_iteration_cost = DoInnerIterations(new_cost,
                                                             &x_plus_delta_);
      if (inner_iteration_cost < std::numeric_limits<double>::max()) {
        VLOG_IF(2, is_not_silent_)
            << "Inner iteration succeeded; Current cost: " << cost_
            << " Trust region step cost: " << new_cost
            << " Inner iteration cost: " << inner_iteration_cost;

        model_cost_change_ +=  new_cost - inner_iteration_cost;
        inner_iterations_were_useful_ = inner_iteration_cost < cost_;
        const double inner_iteration_relative_progress =
          1.0 - inner_iteration_cost / new_cost;

        // Disable inner iterations once the relative improvement
        // drops below tolerance.
        inner_iterations_are_enabled_ = (inner_iteration_relative_progress >
                                         options.inner_iteration_tolerance);
        VLOG_IF(2, is_not_silent_ && !inner_iterations_are_enabled_)
            << "Disabling inner iterations. Progress : "
            << inner_iteration_relative_progress;
        new_cost = inner_iteration_cost;
      }
    }

    iteration_summary_.cost_change =  cost_ - new_cost;
    iteration_summary_.step_norm = (x_ - x_plus_delta_).norm();

    // Convergence based on parameter_tolerance.
    const double step_size_tolerance =  options_.parameter_tolerance *
        (x_norm_ + options_.parameter_tolerance);
    if (iteration_summary_.step_norm <= step_size_tolerance) {
      solver_summary->message =
          StringPrintf("Parameter tolerance reached. "
                       "Relative step_norm: %e <= %e.",
                       (iteration_summary_.step_norm /
                        (x_norm_ + options_.parameter_tolerance)),
                       options_.parameter_tolerance);
      solver_summary->termination_type = CONVERGENCE;
      VLOG_IF(1, is_not_silent_) << "Terminating: " << solver_summary->message;
      return;
    }

    const double absolute_function_tolerance =
        options_.function_tolerance * cost_;
    if (fabs(iteration_summary_.cost_change) <= absolute_function_tolerance) {
      solver_summary->message =
          StringPrintf("Function tolerance reached. "
                       "|cost_change|/cost: %e <= %e",
                       fabs(iteration_summary_.cost_change) / cost_,
                       options_.function_tolerance);
      solver_summary->termination_type = CONVERGENCE;
      VLOG_IF(1, is_not_silent_) << "Terminating: " << solver_summary->message;
      return;
    }

    iteration_summary_.relative_decrease =
        step_evaluator_->StepQuality(new_cost, model_cost_change_);

    iteration_summary_.step_is_successful =
        (inner_iterations_were_useful_ ||
         iteration_summary_.relative_decrease >
         options_.min_relative_decrease);

    if (!iteration_summary_.step_is_successful) {
      ++solver_summary->num_unsuccessful_steps;
      strategy_->StepRejected(iteration_summary_.relative_decrease);
      iteration_summary_.cost = new_cost + solver_summary->fixed_cost;
      continue;
    }

    ++solver_summary->num_successful_steps;
    strategy_->StepAccepted(iteration_summary_.relative_decrease);
    // TODO(sameeragarwal): We need to mark the step monotonic if that
    // is the case.
    step_evaluator_->StepAccepted(new_cost, model_cost_change_);

    x_ = x_plus_delta_;
    x_norm_ = x_.norm();
    RETURN_IF_ERROR_AND_LOG(EvaluateGradientAndJacobian());

    if (cost_ < minimum_cost) {
      minimum_cost = cost_;
      VectorRef(parameters, num_parameters_) = x_;
    }
  }
}

void TrustRegionMinimizer::Init(const Minimizer::Options& options,
                                const double* initial_x,
                                Solver::Summary* solver_summary) {
  options_ = options;
  sort(options_.trust_region_minimizer_iterations_to_dump.begin(),
       options_.trust_region_minimizer_iterations_to_dump.end());

  solver_summary_ = solver_summary;
  solver_summary_->termination_type = NO_CONVERGENCE;
  solver_summary_->num_successful_steps = 0;
  solver_summary_->num_unsuccessful_steps = 0;
  solver_summary_->is_constrained = options.is_constrained;

  evaluator_ = CHECK_NOTNULL(options_.evaluator.get());
  jacobian_ = CHECK_NOTNULL(options_.jacobian.get());
  strategy_ = CHECK_NOTNULL(options_.trust_region_strategy.get());

  is_not_silent_ = !options.is_silent;
  inner_iterations_are_enabled_ =
      options.inner_iteration_minimizer.get() != NULL;
  inner_iterations_were_useful_ = false;

  num_parameters_ = evaluator_->NumParameters();
  num_effective_parameters_ = evaluator_->NumEffectiveParameters();
  num_residuals_ = evaluator_->NumResiduals();
  num_consecutive_invalid_steps_ = 0;

  x_ = ConstVectorRef(initial_x, num_parameters_);
  x_norm_ = x_.norm();
  residuals_.resize(num_residuals_);
  trust_region_step_.resize(num_effective_parameters_);
  delta_.resize(num_effective_parameters_);
  x_plus_delta_.resize(num_parameters_);
  gradient_.resize(num_effective_parameters_);
  model_residuals_.resize(num_residuals_);
  scale_ = Vector::Ones(num_effective_parameters_);
  negative_gradient_.resize(num_effective_parameters_);
  projected_gradient_step_.resize(num_parameters_);
}

bool TrustRegionMinimizer::IterationZero() {
  iteration_summary_ = IterationSummary();
  iteration_summary_.iteration = 0;
  iteration_summary_.step_is_valid = false;
  iteration_summary_.step_is_successful = false;
  iteration_summary_.cost_change = 0.0;
  iteration_summary_.gradient_max_norm = 0.0;
  iteration_summary_.gradient_norm = 0.0;
  iteration_summary_.step_norm = 0.0;
  iteration_summary_.relative_decrease = 0.0;
  iteration_summary_.eta = options_.eta;
  iteration_summary_.linear_solver_iterations = 0;
  iteration_summary_.step_solver_time_in_seconds = 0;

  if (options_.is_constrained) {
    delta_.setZero();
    if (!evaluator_->Plus(x_.data(), delta_.data(), x_plus_delta_.data())) {
      solver_summary_->message =
          "Unable to project initial point onto the feasible set.";
      solver_summary_->termination_type = FAILURE;
      return false;
    }

    x_ = x_plus_delta_;
    x_norm_ = x_.norm();
  }

  if (!EvaluateGradientAndJacobian()) {
    return false;
  }

  solver_summary_->initial_cost = cost_ + solver_summary_->fixed_cost;
  return true;
}

bool TrustRegionMinimizer::EvaluateGradientAndJacobian() {
  if (!evaluator_->Evaluate(x_.data(),
                            &cost_,
                            residuals_.data(),
                            gradient_.data(),
                            jacobian_)) {
    solver_summary_->message = "Residual and Jacobian evaluation failed.";
    solver_summary_->termination_type = FAILURE;
    return false;
  }

  iteration_summary_.cost = cost_ + solver_summary_->fixed_cost;

  if (options_.jacobi_scaling) {
    if (iteration_summary_.iteration == 0) {
      // Compute a scaling vector that is used to improve the
      // conditioning of the Jacobian.
      jacobian_->SquaredColumnNorm(scale_.data());
      for (int i = 0; i < jacobian_->num_cols(); ++i) {
        scale_[i] = 1.0 / (1.0 + sqrt(scale_[i]));
      }
    }
    jacobian_->ScaleColumns(scale_.data());
  }

  negative_gradient_ = -gradient_;
  if (!evaluator_->Plus(x_.data(),
                        negative_gradient_.data(),
                        projected_gradient_step_.data())) {
    solver_summary_->message =
        "projected_gradient_step = Plus(x, -gradient) failed.";
    solver_summary_->termination_type = FAILURE;
    return false;
  }

  iteration_summary_.gradient_max_norm =
      (x_ - projected_gradient_step_).lpNorm<Eigen::Infinity>();
  iteration_summary_.gradient_norm = (x_ - projected_gradient_step_).norm();
  return true;
}

bool TrustRegionMinimizer::FinalizeIterationAndCheckIfMinimizerCanContinue() {
  iteration_summary_.trust_region_radius = strategy_->Radius();
  iteration_summary_.iteration_time_in_seconds =
      WallTimeInSeconds() - iteration_start_time_;
  iteration_summary_.cumulative_time_in_seconds =
      WallTimeInSeconds() - start_time_
      + solver_summary_->preprocessor_time_in_seconds;

  solver_summary_->iterations.push_back(iteration_summary_);
  if (!RunCallbacks(options_, iteration_summary_, solver_summary_)) {
    return false;
  }

  const double total_solver_time = WallTimeInSeconds() - start_time_ +
      solver_summary_->preprocessor_time_in_seconds;
  if (total_solver_time >= options_.max_solver_time_in_seconds) {
    solver_summary_->message = "Maximum solver time reached.";
    solver_summary_->termination_type = NO_CONVERGENCE;
    VLOG_IF(1, is_not_silent_) << "Terminating: " << solver_summary_->message;
    return false;
  }

  if (iteration_summary_.iteration >= options_.max_num_iterations) {
    solver_summary_->message = "Maximum number of iterations reached.";
    solver_summary_->termination_type = NO_CONVERGENCE;
    VLOG_IF(1, is_not_silent_) << "Terminating: " << solver_summary_->message;
    return false;
  }

  if ((iteration_summary_.step_is_successful ||
       iteration_summary_.iteration == 0) &&
      iteration_summary_.gradient_max_norm <= options_.gradient_tolerance) {
    solver_summary_->message =
        StringPrintf("Gradient tolerance reached. "
                     "Gradient max norm: %e <= %e",
                     iteration_summary_.gradient_max_norm,
                     options_.gradient_tolerance);
    solver_summary_->termination_type = CONVERGENCE;
    VLOG_IF(1, is_not_silent_) << "Terminating: " << solver_summary_->message;
    return false;
  }

  if (iteration_summary_.trust_region_radius <
      options_.min_trust_region_radius) {
    solver_summary_->message =
        "Termination. Minimum trust region radius reached.";
    solver_summary_->termination_type = CONVERGENCE;
    VLOG_IF(1, is_not_silent_) << "Terminating: " << solver_summary_->message;
    return false;
  }

  return true;
}

bool TrustRegionMinimizer::ComputeTrustRegionStep() {
  const double strategy_start_time = WallTimeInSeconds();
  TrustRegionStrategy::PerSolveOptions per_solve_options;
  per_solve_options.eta = options_.eta;
  if (find(options_.trust_region_minimizer_iterations_to_dump.begin(),
           options_.trust_region_minimizer_iterations_to_dump.end(),
           iteration_summary_.iteration) !=
      options_.trust_region_minimizer_iterations_to_dump.end()) {
    per_solve_options.dump_format_type =
        options_.trust_region_problem_dump_format_type;
    per_solve_options.dump_filename_base =
        JoinPath(options_.trust_region_problem_dump_directory,
                 StringPrintf("ceres_solver_iteration_%03d",
                              iteration_summary_.iteration));
  }

  TrustRegionStrategy::Summary strategy_summary =
      strategy_->ComputeStep(per_solve_options,
                             jacobian_,
                             residuals_.data(),
                             trust_region_step_.data());

  if (strategy_summary.termination_type == LINEAR_SOLVER_FATAL_ERROR) {
    solver_summary_->message =
        "Linear solver failed due to unrecoverable "
        "non-numeric causes. Please see the error log for clues. ";
    solver_summary_->termination_type = FAILURE;
    return false;
  }

  iteration_summary_.step_solver_time_in_seconds =
      WallTimeInSeconds() - strategy_start_time;
  iteration_summary_.linear_solver_iterations =
      strategy_summary.num_iterations;

  if (strategy_summary.termination_type == LINEAR_SOLVER_FAILURE) {
    iteration_summary_.step_is_valid = false;
    return true;
  }

  // new_model_cost
  //  = 1/2 [f + J * step]^2
  //  = 1/2 [ f'f + 2f'J * step + step' * J' * J * step ]
  // model_cost_change
  //  = cost - new_model_cost
  //  = f'f/2  - 1/2 [ f'f + 2f'J * step + step' * J' * J * step]
  //  = -f'J * step - step' * J' * J * step / 2
  //  = -(J * step)'(f + J * step / 2)
  model_residuals_.setZero();
  jacobian_->RightMultiply(trust_region_step_.data(),
                           model_residuals_.data());
  model_cost_change_ =
      - model_residuals_.dot(residuals_ + model_residuals_ / 2.0);

  // TODO(sameeragarwal)
  //
  // 1. What happens if model_cost_change_ = 0
  // 2. What happens if -epsilon <= model_cost_change_ < 0 for some
  // small epsilon due to round off error.
  iteration_summary_.step_is_valid = (model_cost_change_ > 0.0);
  VLOG_IF(1, is_not_silent_ && !iteration_summary_.step_is_valid)
      << "Invalid step: current_cost: " << cost_
      << " absolute model cost change: " << model_cost_change_
      << " relative model cost change: " << (model_cost_change_ / cost_);
  return true;
}

bool TrustRegionMinimizer::HandleInvalidStep() {
  // Invalid steps can happen due to a number of reasons, and we
  // allow a limited number of successive failures, and return with
  // FAILURE if this limit is exceeded.
  //
  // TODO(sameeragarwal): Should we be returning FAILURE or
  // NO_CONVERGENCE? The solution value is still usable in many cases,
  // it is not clear if we should declare the solver a failure
  // entirely. For example the case where model_cost_change ~ 0.0, but
  // just slightly negative.
  if (++num_consecutive_invalid_steps_ >=
      options_.max_num_consecutive_invalid_steps) {
    solver_summary_->message = StringPrintf(
        "Number of successive invalid steps more "
        "than Solver::Options::max_num_consecutive_invalid_steps: %d",
        options_.max_num_consecutive_invalid_steps);
    solver_summary_->termination_type = FAILURE;
    return false;
  }

  ++solver_summary_->num_unsuccessful_steps;
  strategy_->StepIsInvalid();

  // We are going to try and reduce the trust region radius and
  // solve again. To do this, we are going to treat this iteration
  // as an unsuccessful iteration. Since the various callbacks are
  // still executed, we are going to fill the iteration summary
  // with data that assumes a step of length zero and no progress.
  iteration_summary_.cost = cost_ + solver_summary_->fixed_cost;
  iteration_summary_.cost_change = 0.0;
  iteration_summary_.gradient_max_norm =
      solver_summary_->iterations.back().gradient_max_norm;
  iteration_summary_.gradient_norm =
      solver_summary_->iterations.back().gradient_norm;
  iteration_summary_.step_norm = 0.0;
  iteration_summary_.relative_decrease = 0.0;
  iteration_summary_.eta = options_.eta;
  return true;
}

double TrustRegionMinimizer::DoInnerIterations(const double current_cost,
                                               Vector* x) {
  double inner_iteration_start_time = WallTimeInSeconds();
  ++solver_summary_->num_inner_iteration_steps;
  Vector inner_iteration_x = *x;
  Solver::Summary inner_iteration_summary;
  options_.inner_iteration_minimizer->Minimize(options_,
                                               inner_iteration_x.data(),
                                               &inner_iteration_summary);
  double inner_iteration_cost;
  if (!evaluator_->Evaluate(inner_iteration_x.data(),
                            &inner_iteration_cost,
                            NULL, NULL, NULL)) {
    VLOG_IF(2, is_not_silent_) << "Inner iteration failed.";
    inner_iteration_cost = std::numeric_limits<double>::max();
  } else {
     *x = inner_iteration_x;
  }

  solver_summary_->inner_iteration_time_in_seconds +=
      WallTimeInSeconds() - inner_iteration_start_time;
  return inner_iteration_cost;
}

void TrustRegionMinimizer::DoLineSearch(const Vector& x,
                                        const Vector& gradient,
                                        const double cost,
                                        Vector* delta) {
  LineSearchFunction line_search_function(evaluator_);

  LineSearch::Options line_search_options;
  line_search_options.is_silent = true;
  line_search_options.interpolation_type =
      options_.line_search_interpolation_type;
  line_search_options.min_step_size = options_.min_line_search_step_size;
  line_search_options.sufficient_decrease =
      options_.line_search_sufficient_function_decrease;
  line_search_options.max_step_contraction =
      options_.max_line_search_step_contraction;
  line_search_options.min_step_contraction =
      options_.min_line_search_step_contraction;
  line_search_options.max_num_iterations =
      options_.max_num_line_search_step_size_iterations;
  line_search_options.sufficient_curvature_decrease =
      options_.line_search_sufficient_curvature_decrease;
  line_search_options.max_step_expansion =
      options_.max_line_search_step_expansion;
  line_search_options.function = &line_search_function;

  std::string message;
  scoped_ptr<LineSearch> line_search(
      CHECK_NOTNULL(LineSearch::Create(ceres::ARMIJO,
                                       line_search_options,
                                       &message)));
  LineSearch::Summary line_search_summary;
  line_search_function.Init(x, *delta);
  line_search->Search(1.0, cost, gradient.dot(*delta), &line_search_summary);

  solver_summary_->num_line_search_steps += line_search_summary.num_iterations;
  solver_summary_->line_search_cost_evaluation_time_in_seconds +=
      line_search_summary.cost_evaluation_time_in_seconds;
  solver_summary_->line_search_gradient_evaluation_time_in_seconds +=
      line_search_summary.gradient_evaluation_time_in_seconds;
  solver_summary_->line_search_polynomial_minimization_time_in_seconds +=
      line_search_summary.polynomial_minimization_time_in_seconds;
  solver_summary_->line_search_total_time_in_seconds +=
      line_search_summary.total_time_in_seconds;

  if (line_search_summary.success) {
    *delta *= line_search_summary.optimal_step_size;
  }
}

}  // namespace internal
}  // namespace ceres
