//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// <complex>

// template<> class complex<float>
// {
// public:
//     explicit constexpr complex(const complex<double>&);
// };

#include "support/test_complex.h"

ONEDPL_TEST_NUM_MAIN
{
    IF_DOUBLE_SUPPORT(const dpl::complex<double> cd(2.5, 3.5);
                      dpl::complex<float> cf(cd);
                      assert(cf.real() == cd.real());
                      assert(cf.imag() == cd.imag()))

    IF_DOUBLE_SUPPORT(constexpr dpl::complex<double> cd(2.5, 3.5);
                      constexpr dpl::complex<float> cf(cd);
                      static_assert(cf.real() == cd.real(), "");
                      static_assert(cf.imag() == cd.imag(), ""))

  return 0;
}