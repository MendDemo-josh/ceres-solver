// Ceres Solver - A fast non-linear least squares minimizer
// Copyright 2012 Google Inc. All rights reserved.
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
// Author: strandmark@google.com (Petter Strandmark)
//
// A simple C++ interface to CXSparse, enabling solving linear system with
// caching of the symbolic Cholesky factorization.

#ifndef CERES_INTERNAL_CXSPARSE_H_
#define CERES_INTERNAL_CXSPARSE_H_

#ifndef CERES_NO_CXSPARSE

#include <cstring>
#include <string>
#include <vector>

#include "cs.h"
#include "ceres/internal/port.h"

namespace ceres {
namespace internal {

// This object provides access to solving a linear system using Cholesky
// factorization with a known symbolic factorization. This features does not
// explicity exist in CXSparse. The methods in the class are nonstatic because
// the class manages internal scratch space.
class CXSparse {
 public:
  CXSparse();
  ~CXSparse();

  // Solves a linear system A * x = b using Cholesky factorization.
  //  A      - The system matrix.
  //  factor - The symbolic factorization of A. This is obtained from the
  //           function cs_schol in CXSparse.
  //  b      - The right hand size of the linear equation. This array will also
  //           recieve the solution.
  // Returns false if Cholesky factorization of A fails.
  bool SolveCholesky(cs_di* A, css* factor, double* b);

 private:
  // Cached scratch space
  CS_ENTRY* scratch_;
  int scratch_size_;
};

}  // namespace internal
}  // namespace ceres

#endif  // CERES_NO_CXSPARSE

#endif  // CERES_INTERNAL_CXSPARSE_H_
