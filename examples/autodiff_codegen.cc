// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2015 Google Inc. All rights reserved.
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
// Author: darius.rueckert@fau.com (Darius Rueckert)
//
// A simple example of using AutoDiffCodeGen
//

#include "ceres/autodiff_codegen.h"
#include "glog/logging.h"

using ceres::AutoDiffCostFunction;
using ceres::CostFunction;

struct CostFunctorSimple {
  template <typename T>
  bool operator()(const T* const x, T* residual) const {
    residual[0] = x[0] * x[0];
    return true;
  }
};

struct CostFunctor {
  template <typename T>
  bool operator()(const T* const x, const T* const y, T* residual) const {
    T local = CERES_EXTERNAL_CONSTANT(localVariable);
    residual[0] = T(10.0) - x[0] + x[0] * y[1] / sin(y[0]) + T(3) * exp(x[0]);
    residual[1] = local * sin(x[0]) + sin(x[0]);
    return true;
  }

  double localVariable = 10;
};

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);

  ceres::AutoDiffCodeGen<CostFunctorSimple, 1, 1> codeGen(
      new CostFunctorSimple());
  codeGen.Generate();

  ceres::AutoDiffCodeGen<CostFunctor, 2, 1, 2> codeGen2(new CostFunctor());
  codeGen2.Generate();

  return 0;
}
