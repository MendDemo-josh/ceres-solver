// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2010, 2011, 2012 Google Inc. All rights reserved.
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
// Author: sameeragarwal@google.com (Sameer Agarwal)

#include <cmath>
#include "ceres/autodiff_local_parameterization.h"
#include "ceres/fpclassify.h"
#include "ceres/internal/autodiff.h"
#include "ceres/internal/eigen.h"
#include "ceres/local_parameterization.h"
#include "ceres/rotation.h"
#include "gtest/gtest.h"

namespace ceres {
namespace internal {

TEST(IdentityParameterization, EverythingTest) {
  IdentityParameterization parameterization(3);
  EXPECT_EQ(parameterization.GlobalSize(), 3);
  EXPECT_EQ(parameterization.LocalSize(), 3);

  double x[3] = {1.0, 2.0, 3.0};
  double delta[3] = {0.0, 1.0, 2.0};
  double x_plus_delta[3] = {0.0, 0.0, 0.0};
  parameterization.Plus(x, delta, x_plus_delta);
  EXPECT_EQ(x_plus_delta[0], 1.0);
  EXPECT_EQ(x_plus_delta[1], 3.0);
  EXPECT_EQ(x_plus_delta[2], 5.0);

  double jacobian[9];
  parameterization.ComputeJacobian(x, jacobian);
  int k = 0;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j, ++k) {
      EXPECT_EQ(jacobian[k], (i == j) ? 1.0 : 0.0);
    }
  }

  Matrix global_matrix = Matrix::Ones(10, 3);
  Matrix local_matrix = Matrix::Zero(10, 3);
  parameterization.MultiplyByJacobian(x,
                                      10,
                                      global_matrix.data(),
                                      local_matrix.data());
  EXPECT_EQ((local_matrix - global_matrix).norm(), 0.0);
}

TEST(SubsetParameterization, DeathTests) {
  std::vector<int> constant_parameters;
  EXPECT_DEATH_IF_SUPPORTED(
      SubsetParameterization parameterization(1, constant_parameters),
      "at least");

  constant_parameters.push_back(0);
  EXPECT_DEATH_IF_SUPPORTED(
      SubsetParameterization parameterization(1, constant_parameters),
      "Number of parameters");

  constant_parameters.push_back(1);
  EXPECT_DEATH_IF_SUPPORTED(
      SubsetParameterization parameterization(2, constant_parameters),
      "Number of parameters");

  constant_parameters.push_back(1);
  EXPECT_DEATH_IF_SUPPORTED(
      SubsetParameterization parameterization(2, constant_parameters),
      "duplicates");
}

TEST(SubsetParameterization, NormalFunctionTest) {
  const int kGlobalSize = 4;
  const int kLocalSize = 3;

  double x[kGlobalSize] = {1.0, 2.0, 3.0, 4.0};
  for (int i = 0; i < kGlobalSize; ++i) {
    std::vector<int> constant_parameters;
    constant_parameters.push_back(i);
    SubsetParameterization parameterization(kGlobalSize, constant_parameters);
    double delta[kLocalSize] = {1.0, 2.0, 3.0};
    double x_plus_delta[kGlobalSize] = {0.0, 0.0, 0.0};

    parameterization.Plus(x, delta, x_plus_delta);
    int k = 0;
    for (int j = 0; j < kGlobalSize; ++j) {
      if (j == i)  {
        EXPECT_EQ(x_plus_delta[j], x[j]);
      } else {
        EXPECT_EQ(x_plus_delta[j], x[j] + delta[k++]);
      }
    }

    double jacobian[kGlobalSize * kLocalSize];
    parameterization.ComputeJacobian(x, jacobian);
    int delta_cursor = 0;
    int jacobian_cursor = 0;
    for (int j = 0; j < kGlobalSize; ++j) {
      if (j != i) {
        for (int k = 0; k < kLocalSize; ++k, jacobian_cursor++) {
          EXPECT_EQ(jacobian[jacobian_cursor], delta_cursor == k ? 1.0 : 0.0);
        }
        ++delta_cursor;
      } else {
        for (int k = 0; k < kLocalSize; ++k, jacobian_cursor++) {
          EXPECT_EQ(jacobian[jacobian_cursor], 0.0);
        }
      }
    }

    Matrix global_matrix = Matrix::Ones(10, kGlobalSize);
    for (int row = 0; row < kGlobalSize; ++row) {
      for (int col = 0; col < kGlobalSize; ++col) {
        global_matrix(row, col) = col;
      }
    }

    Matrix local_matrix = Matrix::Zero(10, kLocalSize);
    parameterization.MultiplyByJacobian(x,
                                        10,
                                        global_matrix.data(),
                                        local_matrix.data());
    Matrix expected_local_matrix =
        global_matrix * MatrixRef(jacobian, kGlobalSize, kLocalSize);
    EXPECT_EQ((local_matrix - expected_local_matrix).norm(), 0.0);
  }
}

// Functor needed to implement automatically differentiated Plus for
// quaternions.
struct QuaternionPlus {
  template<typename T>
  bool operator()(const T* x, const T* delta, T* x_plus_delta) const {
    const T squared_norm_delta =
        delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];

    T q_delta[4];
    if (squared_norm_delta > T(0.0)) {
      T norm_delta = sqrt(squared_norm_delta);
      const T sin_delta_by_delta = sin(norm_delta) / norm_delta;
      q_delta[0] = cos(norm_delta);
      q_delta[1] = sin_delta_by_delta * delta[0];
      q_delta[2] = sin_delta_by_delta * delta[1];
      q_delta[3] = sin_delta_by_delta * delta[2];
    } else {
      // We do not just use q_delta = [1,0,0,0] here because that is a
      // constant and when used for automatic differentiation will
      // lead to a zero derivative. Instead we take a first order
      // approximation and evaluate it at zero.
      q_delta[0] = T(1.0);
      q_delta[1] = delta[0];
      q_delta[2] = delta[1];
      q_delta[3] = delta[2];
    }

    QuaternionProduct(q_delta, x, x_plus_delta);
    return true;
  }
};

void QuaternionParameterizationTestHelper(const double* x,
                                          const double* delta,
                                          const double* q_delta) {
  const int kGlobalSize = 4;
  const int kLocalSize = 3;

  const double kTolerance = 1e-14;
  double x_plus_delta_ref[kGlobalSize] = {0.0, 0.0, 0.0, 0.0};
  QuaternionProduct(q_delta, x, x_plus_delta_ref);

  double x_plus_delta[kGlobalSize] = {0.0, 0.0, 0.0, 0.0};
  QuaternionParameterization parameterization;
  parameterization.Plus(x, delta, x_plus_delta);
  for (int i = 0; i < kGlobalSize; ++i) {
    EXPECT_NEAR(x_plus_delta[i], x_plus_delta_ref[i], kTolerance);
  }

  const double x_plus_delta_norm =
      sqrt(x_plus_delta[0] * x_plus_delta[0] +
           x_plus_delta[1] * x_plus_delta[1] +
           x_plus_delta[2] * x_plus_delta[2] +
           x_plus_delta[3] * x_plus_delta[3]);

  EXPECT_NEAR(x_plus_delta_norm, 1.0, kTolerance);

  double jacobian_ref[12];
  double zero_delta[kLocalSize] = {0.0, 0.0, 0.0};
  const double* parameters[2] = {x, zero_delta};
  double* jacobian_array[2] = { NULL, jacobian_ref };

  // Autodiff jacobian at delta_x = 0.
  internal::AutoDiff<QuaternionPlus,
                     double,
                     kGlobalSize,
                     kLocalSize>::Differentiate(QuaternionPlus(),
                                                parameters,
                                                kGlobalSize,
                                                x_plus_delta,
                                                jacobian_array);

  double jacobian[12];
  parameterization.ComputeJacobian(x, jacobian);
  for (int i = 0; i < 12; ++i) {
    EXPECT_TRUE(IsFinite(jacobian[i]));
    EXPECT_NEAR(jacobian[i], jacobian_ref[i], kTolerance)
        << "Jacobian mismatch: i = " << i
        << "\n Expected \n"
        << ConstMatrixRef(jacobian_ref, kGlobalSize, kLocalSize)
        << "\n Actual \n"
        << ConstMatrixRef(jacobian, kGlobalSize, kLocalSize);
  }

  Matrix global_matrix = Matrix::Random(10, kGlobalSize);
  Matrix local_matrix = Matrix::Zero(10, kLocalSize);
  parameterization.MultiplyByJacobian(x,
                                      10,
                                      global_matrix.data(),
                                      local_matrix.data());
  Matrix expected_local_matrix =
      global_matrix * MatrixRef(jacobian, kGlobalSize, kLocalSize);
  EXPECT_EQ((local_matrix - expected_local_matrix).norm(), 0.0);
}

template <int N>
void normalize(double* x) {
  double norm_x = 0.0;
  for (int i = 0; i < N; ++i)
    norm_x += x[i] * x[i];
  norm_x = sqrt(norm_x);

  for (int i = 0; i < N; ++i)
    x[i] /= norm_x;
}

TEST(QuaternionParameterization, ZeroTest) {
  double x[4] = {0.5, 0.5, 0.5, 0.5};
  double delta[3] = {0.0, 0.0, 0.0};
  double q_delta[4] = {1.0, 0.0, 0.0, 0.0};
  QuaternionParameterizationTestHelper(x, delta, q_delta);
}


TEST(QuaternionParameterization, NearZeroTest) {
  double x[4] = {0.52, 0.25, 0.15, 0.45};
  normalize<4>(x);

  double delta[3] = {0.24, 0.15, 0.10};
  for (int i = 0; i < 3; ++i) {
    delta[i] = delta[i] * 1e-14;
  }

  double q_delta[4];
  q_delta[0] = 1.0;
  q_delta[1] = delta[0];
  q_delta[2] = delta[1];
  q_delta[3] = delta[2];

  QuaternionParameterizationTestHelper(x, delta, q_delta);
}

TEST(QuaternionParameterization, AwayFromZeroTest) {
  double x[4] = {0.52, 0.25, 0.15, 0.45};
  normalize<4>(x);

  double delta[3] = {0.24, 0.15, 0.10};
  const double delta_norm = sqrt(delta[0] * delta[0] +
                                 delta[1] * delta[1] +
                                 delta[2] * delta[2]);
  double q_delta[4];
  q_delta[0] = cos(delta_norm);
  q_delta[1] = sin(delta_norm) / delta_norm * delta[0];
  q_delta[2] = sin(delta_norm) / delta_norm * delta[1];
  q_delta[3] = sin(delta_norm) / delta_norm * delta[2];

  QuaternionParameterizationTestHelper(x, delta, q_delta);
}

// Compute the Householder vector for vectors of size 4.
template <typename Scalar>
void ComputeHouseholderVector(const Scalar* x, Scalar* v, Scalar* beta) {
  CHECK_NOTNULL(x);
  CHECK_NOTNULL(v);
  CHECK_NOTNULL(beta);

  const int kLength = 4;
  Scalar sigma = Scalar(0.0);
  for (int i = 0; i < kLength - 1; ++i) {
    sigma += x[i] * x[i];
    v[i] = x[i];
  }
  v[kLength - 1] = Scalar(1.0);

  *beta = Scalar(0.0);
  const Scalar& x_pivot = x[kLength - 1];

  if (sigma <= Scalar(std::numeric_limits<double>::epsilon())) {
    if (x_pivot < Scalar(0.0))
      *beta = Scalar(-2.0);
  } else {
    const Scalar mu = sqrt(x_pivot * x_pivot + sigma);
    Scalar v_pivot = v[kLength - 1];

    if (x_pivot <= Scalar(0.0))
      v_pivot = x_pivot - mu;
    else
      v_pivot = -sigma / (x_pivot + mu);

    *beta = Scalar(2.0) * v_pivot * v_pivot / (sigma + v_pivot * v_pivot);

    for (int i = 0; i < kLength - 1; ++i) {
      v[i] /= v_pivot;
    }
  }
}

// Functor needed to implement automatically differentiated Plus for
// homogeneous vectors. Note this explicitly defined for vectors of size 4.
struct HomogeneousVectorParameterizationPlus {
  template<typename Scalar>
  bool operator()(const Scalar* p_x, const Scalar* p_delta,
                  Scalar* p_x_plus_delta) const {
    Eigen::Map<const Eigen::Matrix<Scalar, 4, 1> > x(p_x);
    Eigen::Map<const Eigen::Matrix<Scalar, 3, 1> > delta(p_delta);
    Eigen::Map<Eigen::Matrix<Scalar, 4, 1> > x_plus_delta(p_x_plus_delta);

    const Scalar squared_norm_delta =
        delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];

    Eigen::Matrix<Scalar, 4, 1> y;
    Scalar one_half(0.5);
    if (squared_norm_delta > Scalar(0.0)) {
      Scalar norm_delta = sqrt(squared_norm_delta);
      Scalar norm_delta_div_2 = 0.5 * norm_delta;
      const Scalar sin_delta_by_delta = sin(norm_delta_div_2) /
          norm_delta_div_2;
      y[0] = sin_delta_by_delta * delta[0] * one_half;
      y[1] = sin_delta_by_delta * delta[1] * one_half;
      y[2] = sin_delta_by_delta * delta[2] * one_half;
      y[3] = cos(norm_delta_div_2);

    } else {
      // We do not just use y = [0,0,0,1] here because that is a
      // constant and when used for automatic differentiation will
      // lead to a zero derivative. Instead we take a first order
      // approximation and evaluate it at zero.
      y[0] = delta[0] * one_half;
      y[1] = delta[1] * one_half;
      y[2] = delta[2] * one_half;
      y[3] = Scalar(1.0);
    }

    Scalar v[4];
    Scalar beta;
    ComputeHouseholderVector(p_x, v, &beta);

    Eigen::Map<const Eigen::Matrix<Scalar, 4, 1> > v_eigen(v);
    x_plus_delta = y - beta * v_eigen * v_eigen.dot(y);

    return true;
  }
};

void HomogeneousVectorParameterizationHelper(const double* x,
                                             const double* delta) {
  const double kTolerance = 1e-14;

  HomogeneousVectorParameterization homogeneous_vector_parameterization(4);

  // Ensure the update maintains the norm = 1 requirement.
  double x_plus_delta[4] = {0.0, 0.0, 0.0, 0.0};
  homogeneous_vector_parameterization.Plus(x, delta, x_plus_delta);

  const double x_plus_delta_norm =
      sqrt(x_plus_delta[0] * x_plus_delta[0] +
           x_plus_delta[1] * x_plus_delta[1] +
           x_plus_delta[2] * x_plus_delta[2] +
           x_plus_delta[3] * x_plus_delta[3]);

  EXPECT_NEAR(x_plus_delta_norm, 1.0, kTolerance);

  // Autodiff jacobian at delta_x = 0.
  AutoDiffLocalParameterization<HomogeneousVectorParameterizationPlus, 4, 3>
      autodiff_jacobian;

  double jacobian_autodiff[12];
  double jacobian_analytic[12];

  homogeneous_vector_parameterization.ComputeJacobian(x, jacobian_analytic);
  autodiff_jacobian.ComputeJacobian(x, jacobian_autodiff);

  for (int i = 0; i < 12; ++i) {
    EXPECT_TRUE(ceres::IsFinite(jacobian_analytic[i]));
    EXPECT_NEAR(jacobian_analytic[i], jacobian_autodiff[i], kTolerance)
        << "Jacobian mismatch: i = " << i << jacobian_analytic[i] << " "
        << jacobian_autodiff[i];
  }
}

TEST(HomogeneousVectorParameterization, ZeroTest) {
  double x[4] = {0.0, 0.0, 0.0, 1.0};
  normalize<4>(x);
  double delta[3] = {0.0, 0.0, 0.0};

  HomogeneousVectorParameterizationHelper(x, delta);
}

TEST(HomogeneousVectorParameterization, NearZeroTest) {
  double x[4] = {1e-5, 1e-5, 1e-5, 1.0};
  normalize<4>(x);
  double delta[3] = {0.0, 1.0, 0.0};

  HomogeneousVectorParameterizationHelper(x, delta);
}

TEST(HomogeneousVectorParameterization, AwayFromZeroTest1) {
  double x[4] = {0.52, 0.25, 0.15, 0.45};
  normalize<4>(x);
  double delta[3] = {0.0, 1.0, -0.5};

  HomogeneousVectorParameterizationHelper(x, delta);
}

TEST(HomogeneousVectorParameterization, AwayFromZeroTest2) {
  double x[4] = {0.87, -0.25, -0.34, 0.45};
  normalize<4>(x);
  double delta[3] = {0.0, 0.0, -0.5};

  HomogeneousVectorParameterizationHelper(x, delta);
}

TEST(HomogeneousVectorParameterization, DeathTests) {
  double x[4] = {0.52, 0.25, 0.15, 1.45};
  double delta[3] = {0.0, 1.0, -0.5};

  EXPECT_DEATH_IF_SUPPORTED(
      HomogeneousVectorParameterizationHelper(x, delta),
      "unit norm");

  EXPECT_DEATH_IF_SUPPORTED(HomogeneousVectorParameterization x(1), "size");
}

}  // namespace internal
}  // namespace ceres
