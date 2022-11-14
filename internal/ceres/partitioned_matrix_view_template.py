# Ceres Solver - A fast non-linear least squares minimizer
# Copyright 2015 Google Inc. All rights reserved.
# http://ceres-solver.org/
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# * Neither the name of Google Inc. nor the names of its contributors may be
#   used to endorse or promote products derived from this software without
#   specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
# Author: sameeragarwal@google.com (Sameer Agarwal)
#
# Script for explicitly generating template specialization of the
# PartitionedMatrixView class. Explicitly generating these
# instantiations in separate .cc files breaks the compilation into
# separate compilation unit rather than one large cc file.
#
# This script creates two sets of files.
#
# 1. partitioned_matrix_view_x_x_x.cc
# where the x indicates the template parameters and
#
# 2. partitioned_matrix_view.cc
#
# that contains a factory function for instantiating these classes
# based on runtime parameters.
#
# The list of tuples, specializations indicates the set of
# specializations that is generated.

HEADER = """// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2017 Google Inc. All rights reserved.
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
//
// Template specialization of PartitionedMatrixView.
//
// ========================================
// THIS FILE IS AUTOGENERATED. DO NOT EDIT.
// THIS FILE IS AUTOGENERATED. DO NOT EDIT.
// THIS FILE IS AUTOGENERATED. DO NOT EDIT.
// THIS FILE IS AUTOGENERATED. DO NOT EDIT.
//=========================================
//
// This file is generated using generate_template_specializations.py.
"""

DYNAMIC_FILE = """
#include "ceres/partitioned_matrix_view_impl.h"

namespace ceres::internal {

template class PartitionedMatrixView<%s,
                                     %s,
                                     %s>;

}  // namespace ceres::internal
"""

SPECIALIZATION_FILE = """
// This include must come before any #ifndef check on Ceres compile options.
#include "ceres/internal/config.h"

#ifndef CERES_RESTRICT_SCHUR_SPECIALIZATION

#include "ceres/partitioned_matrix_view_impl.h"

namespace ceres::internal {

template class PartitionedMatrixView<%s, %s, %s>;

}  // namespace ceres::internal

#endif  // CERES_RESTRICT_SCHUR_SPECIALIZATION
"""

FACTORY_FILE_HEADER = """
#include <memory>

#include "ceres/linear_solver.h"
#include "ceres/partitioned_matrix_view.h"

namespace ceres::internal {

PartitionedMatrixViewBase::~PartitionedMatrixViewBase() = default;

std::unique_ptr<PartitionedMatrixViewBase> PartitionedMatrixViewBase::Create(
    const LinearSolver::Options& options, const BlockSparseMatrix& matrix) {
#ifndef CERES_RESTRICT_SCHUR_SPECIALIZATION
"""
FACTORY = """  return std::make_unique<PartitionedMatrixView<%s,%s, %s>>(
                   options, matrix);"""

FACTORY_FOOTER = """
#endif
  VLOG(1) << "Template specializations not found for <"
          << options.row_block_size << "," << options.e_block_size << ","
          << options.f_block_size << ">";
  return std::make_unique<PartitionedMatrixView<Eigen::Dynamic,
                                                Eigen::Dynamic,
                                                Eigen::Dynamic>>(
      options, matrix);
};

}  // namespace ceres::internal
"""
