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

#ifndef CERES_NO_SUITESPARSE


#include "ceres/suitesparse.h"

#include <numeric>
#include "cholmod.h"
#include "ceres/compressed_row_sparse_matrix.h"
#include "ceres/triplet_sparse_matrix.h"
namespace ceres {
namespace internal {
cholmod_sparse* SuiteSparse::CreateSparseMatrix(TripletSparseMatrix* A) {
  cholmod_triplet triplet;

  triplet.nrow = A->num_rows();
  triplet.ncol = A->num_cols();
  triplet.nzmax = A->max_num_nonzeros();
  triplet.nnz = A->num_nonzeros();
  triplet.i = reinterpret_cast<void*>(A->mutable_rows());
  triplet.j = reinterpret_cast<void*>(A->mutable_cols());
  triplet.x = reinterpret_cast<void*>(A->mutable_values());
  triplet.stype = 0;  // Matrix is not symmetric.
  triplet.itype = CHOLMOD_INT;
  triplet.xtype = CHOLMOD_REAL;
  triplet.dtype = CHOLMOD_DOUBLE;

  return cholmod_triplet_to_sparse(&triplet, triplet.nnz, &cc_);
}


cholmod_sparse* SuiteSparse::CreateSparseMatrixTranspose(
    TripletSparseMatrix* A) {
  cholmod_triplet triplet;

  triplet.ncol = A->num_rows();  // swap row and columns
  triplet.nrow = A->num_cols();
  triplet.nzmax = A->max_num_nonzeros();
  triplet.nnz = A->num_nonzeros();

  // swap rows and columns
  triplet.j = reinterpret_cast<void*>(A->mutable_rows());
  triplet.i = reinterpret_cast<void*>(A->mutable_cols());
  triplet.x = reinterpret_cast<void*>(A->mutable_values());
  triplet.stype = 0;  // Matrix is not symmetric.
  triplet.itype = CHOLMOD_INT;
  triplet.xtype = CHOLMOD_REAL;
  triplet.dtype = CHOLMOD_DOUBLE;

  return cholmod_triplet_to_sparse(&triplet, triplet.nnz, &cc_);
}

cholmod_sparse* SuiteSparse::CreateSparseMatrixTransposeView(
    CompressedRowSparseMatrix* A) {
  cholmod_sparse* m = new cholmod_sparse_struct;
  m->nrow = A->num_cols();
  m->ncol = A->num_rows();
  m->nzmax = A->num_nonzeros();

  m->p = reinterpret_cast<void*>(A->mutable_rows());
  m->i = reinterpret_cast<void*>(A->mutable_cols());
  m->x = reinterpret_cast<void*>(A->mutable_values());

  m->stype = 0;  // Matrix is not symmetric.
  m->itype = CHOLMOD_INT;
  m->xtype = CHOLMOD_REAL;
  m->dtype = CHOLMOD_DOUBLE;
  m->sorted = 1;
  m->packed = 1;

  return m;
}

cholmod_dense* SuiteSparse::CreateDenseVector(const double* x,
                                              int in_size,
                                              int out_size) {
    CHECK_LE(in_size, out_size);
    cholmod_dense* v = cholmod_zeros(out_size, 1, CHOLMOD_REAL, &cc_);
    if (x != NULL) {
      memcpy(v->x, x, in_size*sizeof(*x));
    }
    return v;
}

cholmod_factor* SuiteSparse::AnalyzeCholesky(cholmod_sparse* A) {
  // Cholmod can try multiple re-ordering strategies to find a fill
  // reducing ordering. Here we just tell it use AMD with automatic
  // matrix dependence choice of supernodal versus simplicial
  // factorization.
  cc_.nmethods = 1 ;
  cc_.method[0].ordering = CHOLMOD_AMD;
  cc_.supernodal = CHOLMOD_AUTO;
  cholmod_factor* factor = cholmod_analyze(A, &cc_);
  CHECK_EQ(cc_.status, CHOLMOD_OK)
      << "Cholmod symbolic analysis failed " << cc_.status;
  CHECK_NOTNULL(factor);
  return factor;
}

cholmod_factor* SuiteSparse::BlockAnalyzeCholesky(
    cholmod_sparse* A,
    const vector<int>& row_blocks,
    const vector<int>& col_blocks) {
  vector<int> ordering;
  if (!BlockAMDOrdering(A, row_blocks, col_blocks, &ordering)) {
    return NULL;
  }
  return AnalyzeCholeskyWithUserOrdering(A, ordering);
}

cholmod_factor* SuiteSparse::AnalyzeCholeskyWithUserOrdering(cholmod_sparse* A,
                                                             const vector<int>& ordering) {
  CHECK_EQ(ordering.size(), A->nrow);
  cc_.nmethods = 1 ;
  cc_.method[0].ordering = CHOLMOD_GIVEN;
  cholmod_factor* factor  =
      cholmod_analyze_p(A, const_cast<int*>(&ordering[0]), NULL, 0, &cc_);
  CHECK_EQ(cc_.status, CHOLMOD_OK)
      << "Cholmod symbolic analysis failed " << cc_.status;
  CHECK_NOTNULL(factor);
  return factor;
}

bool SuiteSparse::BlockAMDOrdering(const cholmod_sparse* A,
                                   const vector<int>& row_blocks,
                                   const vector<int>& col_blocks,
                                   vector<int>* ordering) {
  const int num_row_blocks = row_blocks.size();
  const int num_col_blocks = col_blocks.size();
  vector<int> row_block_starts(num_row_blocks);
  row_block_starts[0] = 0;
  for (int i = 1; i < num_row_blocks ; ++i) {
    row_block_starts[i] = row_block_starts[i -1] + row_blocks[i - 1];
  }

  // Arrays storing the compressed column structure of the matrix
  // incoding the block sparsity of A.
  vector<int> Bp;  // columns
  vector<int> Bi;  // rows

  const int* Ap =  reinterpret_cast<const int*>(A->p);
  const int* Ai =  reinterpret_cast<const int*>(A->i);
  Bp.push_back(0);
  int c = 0;
  // Only consider the first column in each column block.
  for (int col_block = 0; col_block < num_col_blocks; ++col_block) {
    int column_size = 0;
    for (int idx = Ap[c]; idx < Ap[c + 1]; ++idx) {
      vector<int>::const_iterator it =
          lower_bound(row_block_starts.begin(), row_block_starts.end(), Ai[idx]);
      // Only consider the first row of each row block.
      if (*it != Ai[idx]) {
        continue;
      }

      Bi.push_back(it - row_block_starts.begin());
      ++column_size;
    }
    Bp.push_back(Bp.back() + column_size);
    c += col_blocks[col_block];
  }

  cholmod_sparse_struct block_sparsity;
  block_sparsity.nrow = num_row_blocks;
  block_sparsity.ncol = num_col_blocks;
  block_sparsity.nzmax = Bi.size();
  block_sparsity.p = reinterpret_cast<void*>(&Bp[0]);
  block_sparsity.i = reinterpret_cast<void*>(&Bi[0]);
  block_sparsity.x = NULL;
  block_sparsity.stype = A->stype;
  block_sparsity.itype = CHOLMOD_INT;
  block_sparsity.xtype = CHOLMOD_PATTERN;
  block_sparsity.dtype = CHOLMOD_DOUBLE;
  block_sparsity.sorted = 1;
  block_sparsity.packed = 1;

  vector<int> block_ordering(num_row_blocks);
  if (!cholmod_amd(&block_sparsity, NULL, 0, &block_ordering[0], &cc_)) {
    return false;
  }

  BlockOrderingToScalarOrdering(row_blocks, block_ordering, ordering);
  return true;
}

void SuiteSparse::BlockOrderingToScalarOrdering(
    const vector<int>& blocks,
    const vector<int>& block_ordering,
    vector<int>* scalar_ordering) {
  CHECK_EQ(blocks.size(), block_ordering.size());
  const int num_blocks = blocks.size();

  // block_starts = [0, block1, block1 + block2 ..]
  vector<int> block_start(num_blocks);
  block_start[0] = 0;
  for (int i = 1; i < num_blocks ; ++i) {
    block_start[i] = block_start[i -1] + blocks[i - 1];
  }

  scalar_ordering->resize(block_start.back() + blocks.back());
  int cursor = 0;
  for (int i = 0; i < num_blocks; ++i) {
    const int block_id = block_ordering[i];
    const int block_size = blocks[block_id];
    int block_position = block_start[block_id];
    for ( int j = 0; j < block_size; ++j) {
      (*scalar_ordering)[cursor++] = block_position++;
    }
  }
}

bool SuiteSparse::Cholesky(cholmod_sparse* A, cholmod_factor* L) {
  CHECK_NOTNULL(A);
  CHECK_NOTNULL(L);

  cc_.quick_return_if_not_posdef = 1;
  int status = cholmod_factorize(A, L, &cc_);
  switch (cc_.status) {
    case CHOLMOD_NOT_INSTALLED:
      LOG(WARNING) << "Cholmod failure: method not installed.";
      return false;
    case CHOLMOD_OUT_OF_MEMORY:
      LOG(WARNING) << "Cholmod failure: out of memory.";
      return false;
    case CHOLMOD_TOO_LARGE:
      LOG(WARNING) << "Cholmod failure: integer overflow occured.";
      return false;
    case CHOLMOD_INVALID:
      LOG(WARNING) << "Cholmod failure: invalid input.";
      return false;
    case CHOLMOD_NOT_POSDEF:
      // TODO(sameeragarwal): These two warnings require more
      // sophisticated handling going forward. For now we will be
      // strict and treat them as failures.
      LOG(WARNING) << "Cholmod warning: matrix not positive definite.";
      return false;
    case CHOLMOD_DSMALL:
      LOG(WARNING) << "Cholmod warning: D for LDL' or diag(L) or "
                   << "LL' has tiny absolute value.";
      return false;
    case CHOLMOD_OK:
      if (status != 0) {
        return true;
      }
      LOG(WARNING) << "Cholmod failure: cholmod_factorize returned zero "
                   << "but cholmod_common::status is CHOLMOD_OK."
                   << "Please report this to ceres-solver@googlegroups.com.";
      return false;
    default:
      LOG(WARNING) << "Unknown cholmod return code. "
                   << "Please report this to ceres-solver@googlegroups.com.";
      return false;
  }
  return false;
}

cholmod_dense* SuiteSparse::Solve(cholmod_factor* L,
                                  cholmod_dense* b) {
  if (cc_.status != CHOLMOD_OK) {
    LOG(WARNING) << "CHOLMOD status NOT OK";
    return NULL;
  }

  return cholmod_solve(CHOLMOD_A, L, b, &cc_);
}

cholmod_dense* SuiteSparse::SolveCholesky(cholmod_sparse* A,
                                          cholmod_factor* L,
                                          cholmod_dense* b) {
  CHECK_NOTNULL(A);
  CHECK_NOTNULL(L);
  CHECK_NOTNULL(b);

  if (Cholesky(A, L)) {
    return Solve(L, b);
  }

  return NULL;
}

}  // namespace internal
}  // namespace ceres

#endif  // CERES_NO_SUITESPARSE
