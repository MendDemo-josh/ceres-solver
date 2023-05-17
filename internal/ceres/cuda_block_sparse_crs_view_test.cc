// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2023 Google Inc. All rights reserved.
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
// Authors: dmitriy.korchemkin@gmail.com (Dmitriy Korchemkin)

#include "ceres/cuda_block_sparse_crs_view.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#ifndef CERES_NO_CUDA

namespace ceres::internal {
class CudaBlockSparseCRSViewTest : public ::testing::Test {
 protected:
  void SetUp() final {
    std::string message;
    CHECK(context_.InitCuda(&message))
        << "InitCuda() failed because: " << message;

    BlockSparseMatrix::RandomMatrixOptions options;
    options.num_row_blocks = 1234;
    options.min_row_block_size = 1;
    options.max_row_block_size = 10;
    options.num_col_blocks = 567;
    options.min_col_block_size = 1;
    options.max_col_block_size = 10;
    options.block_density = 0.2;
    std::mt19937 rng;

    // Block-sparse matrix with order of values different from CRS
    A_ = BlockSparseMatrix::CreateRandomMatrix(options, rng, true);
    std::iota(
        A_->mutable_values(), A_->mutable_values() + A_->num_nonzeros(), 1);

    options.max_row_block_size = 1;
    // Block-sparse matrix with CRS order of values
    B_ = BlockSparseMatrix::CreateRandomMatrix(options, rng, true);
    std::iota(
        B_->mutable_values(), B_->mutable_values() + B_->num_nonzeros(), 1);

    const int num_col_block_e = 234;
    auto bs =
        std::make_unique<CompressedRowBlockStructure>(*A_->block_structure());
    int num_nonzeros_e = 0;
    for (const auto& row : bs->rows) {
      for (const auto& cell : row.cells) {
        if (cell.block_id < num_col_block_e) {
          num_nonzeros_e += row.block.size * bs->cols[cell.block_id].size;
        }
      }
    }

    int num_nonzeros_f = num_nonzeros_e;
    num_nonzeros_e = 0;
    for (auto& row : bs->rows) {
      for (auto& cell : row.cells) {
        if (cell.block_id < num_col_block_e) {
          cell.position = num_nonzeros_e;
          num_nonzeros_e += row.block.size * bs->cols[cell.block_id].size;
        } else {
          cell.position = num_nonzeros_f;
          num_nonzeros_f += row.block.size * bs->cols[cell.block_id].size;
        }
      }
    }
    // Partitioned block-sparse matrix
    C_ = std::make_unique<BlockSparseMatrix>(bs.release());
    std::iota(
        C_->mutable_values(), C_->mutable_values() + C_->num_nonzeros(), 1);
  }

  void Compare(const BlockSparseMatrix& bsm, CudaSparseMatrix& csm) {
    ASSERT_EQ(csm.num_cols(), bsm.num_cols());
    ASSERT_EQ(csm.num_rows(), bsm.num_rows());
    ASSERT_EQ(csm.num_nonzeros(), bsm.num_nonzeros());
    const int num_rows = bsm.num_rows();
    const int num_cols = bsm.num_cols();
    Vector x(num_cols);
    Vector y(num_rows);
    CudaVector x_cuda(&context_, num_cols);
    CudaVector y_cuda(&context_, num_rows);
    Vector y_cuda_host(num_rows);

    for (int i = 0; i < num_cols; ++i) {
      x.setZero();
      y.setZero();
      y_cuda.SetZero();
      x[i] = 1.;
      x_cuda.CopyFromCpu(x);
      csm.RightMultiplyAndAccumulate(x_cuda, &y_cuda);
      bsm.RightMultiplyAndAccumulate(
          x.data(), y.data(), &context_, std::thread::hardware_concurrency());
      y_cuda.CopyTo(&y_cuda_host);
      // There will be up to 1 non-zero product per row, thus we expect an exact
      // match on 32-bit integer indices
      EXPECT_EQ((y - y_cuda_host).squaredNorm(), 0.);
    }
  }

  std::unique_ptr<BlockSparseMatrix> A_;
  std::unique_ptr<BlockSparseMatrix> B_;
  std::unique_ptr<BlockSparseMatrix> C_;
  ContextImpl context_;
};

TEST_F(CudaBlockSparseCRSViewTest, CreateUpdateValuesNonCompatible) {
  auto view = CudaBlockSparseCRSView(*A_, &context_);
  ASSERT_EQ(view.crs_compatible(), false);

  auto matrix = view.mutable_crs_matrix();
  Compare(*A_, *matrix);
}

TEST_F(CudaBlockSparseCRSViewTest, CreateUpdateValuesCompatible) {
  auto view = CudaBlockSparseCRSView(*B_, &context_);
  ASSERT_EQ(view.crs_compatible(), true);

  auto matrix = view.mutable_crs_matrix();
  Compare(*B_, *matrix);
}

TEST_F(CudaBlockSparseCRSViewTest, CreateUpdateValuesNonCompatiblePartitioned) {
  auto view = CudaBlockSparseCRSView(*C_, &context_);
  ASSERT_EQ(view.crs_compatible(), false);

  auto matrix = view.mutable_crs_matrix();
  Compare(*C_, *matrix);
}
}  // namespace ceres::internal

#endif  // CERES_NO_CUDA
