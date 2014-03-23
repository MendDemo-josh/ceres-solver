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
// Author: richie.stebbing@gmail.com (Richard Stebbing)

#ifndef CERES_INTERNAL_DYNAMIC_COMPRESSED_ROW_SPARSE_MATRIX_H_
#define CERES_INTERNAL_DYNAMIC_COMPRESSED_ROW_SPARSE_MATRIX_H_

#include "ceres/compressed_row_sparse_matrix.h"

namespace ceres {
namespace internal {

class DynamicCompressedRowSparseMatrix : public CompressedRowSparseMatrix {
 public:
  DynamicCompressedRowSparseMatrix(int num_rows,
                                   int num_cols,
                                   int max_num_nonzeros)
    : CompressedRowSparseMatrix(num_rows, num_cols, max_num_nonzeros) {
    dynamic_cols_.resize(num_rows);
    dynamic_values_.resize(num_rows);
  }

  inline void InsertEntry(int row, int col, const double& value) {
    dynamic_cols_[row].push_back(col);
    dynamic_values_[row].push_back(value);
  }

  inline void ClearRows(int row_start, int num_rows) {
    for (int r = 0; r < num_rows; ++r) {
      dynamic_cols_[row_start + r].resize(0);
      dynamic_values_[row_start + r].resize(0);
    }
  }

  void Finalize(int num_additional=0);

 protected:
  vector<vector<int>> dynamic_cols_;
  vector<vector<double>> dynamic_values_;
};

}  // namespace internal
}  // namespace ceres

#endif // CERES_INTERNAL_DYNAMIC_COMPRESSED_ROW_SPARSE_MATRIX_H_
