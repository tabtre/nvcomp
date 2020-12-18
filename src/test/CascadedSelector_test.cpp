/*
 * Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define CATCH_CONFIG_MAIN

#include "../common.h"
#include "../type_macros.h"
#include "cascaded.hpp"
#include "cascaded.h"
#include "nvcomp.h"
#include "nvcomp.hpp"
#include <algorithm>
#include <assert.h>
#include <cstring>
#include <getopt.h>
#include <vector>

#include "../../tests/catch.hpp"
#include "common.h"
#include "CascadedCommon.h"
#include "cuda_runtime.h"
#include <cstdlib>

#include "../CascadedCompressionGPU.h"

#ifndef CUDA_RT_CALL
#define CUDA_RT_CALL(call)                                                     \
  {                                                                            \
    cudaError_t cudaStatus = call;                                             \
    if (cudaSuccess != cudaStatus) {                                           \
      fprintf(                                                                 \
          stderr,                                                              \
          "ERROR: CUDA RT call \"%s\" in line %d of file %s failed with %s "   \
          "(%d).\n",                                                           \
          #call,                                                               \
          __LINE__,                                                            \
          __FILE__,                                                            \
          cudaGetErrorString(cudaStatus),                                      \
          cudaStatus);                                                         \
      abort();                                                                 \
    }                                                                          \
  }
#endif

using namespace nvcomp;
using namespace std;

/******************************************************************************
 * UNIT TEST ******************************************************************
 *****************************************************************************/

TEST_CASE("Selector_CPP_constructor_getSize", "[small]")
{
  size_t const n = 1000000;
  using T = int32_t;

  T* d_input; 
  const size_t numBytes = n * sizeof(T);
  size_t temp_bytes;

  CUDA_RT_CALL( cudaMalloc(&d_input, numBytes) );

  bool threw_exception=false;
  nvcompCascadedSelectorOpts selector_opts;
  selector_opts.sample_size=1024;
  selector_opts.num_samples=10;

  CUDA_RT_CALL( cudaMalloc(&d_input, numBytes) );

  CascadedSelector<T> selector(d_input, numBytes, selector_opts);

  temp_bytes = selector.get_temp_size();
  REQUIRE(temp_bytes == 120);
}


TEST_CASE("SelectorGetTempSize_C", "[small]")
{

  size_t const n = 1000000;
  using T = int32_t;

  T* d_input; 
  const size_t numBytes = n * sizeof(T);

  CUDA_RT_CALL( cudaMalloc(&d_input, numBytes) );

  size_t temp_bytes = 0;

  nvcompCascadedSelectorOpts selector_opts;
  selector_opts.sample_size = 1024;
  selector_opts.num_samples = 10;

  nvcompError_t err = nvcompCascadedSelectorGetTempSize(numBytes, getnvcompType<T>(), selector_opts, &temp_bytes);
  REQUIRE(err == nvcompSuccess);
  REQUIRE(temp_bytes == 120);

  selector_opts.num_samples = 100;
  err = nvcompCascadedSelectorGetTempSize(numBytes, getnvcompType<T>(), selector_opts, &temp_bytes);
  REQUIRE(err == nvcompSuccess);
  REQUIRE(temp_bytes == 840);

  selector_opts.sample_size = 1;
  selector_opts.num_samples = 1000;
  err = nvcompCascadedSelectorGetTempSize(numBytes, getnvcompType<T>(), selector_opts, &temp_bytes);
  REQUIRE(err == nvcompSuccess);
  REQUIRE(temp_bytes == 8040);

  cudaFree(d_input);
}

TEST_CASE("SelectorSelectConfig_C", "[small]")
{

  size_t const n = 1000000;
  using T = int32_t;

  T* d_input; 
  T* inputHost;
  T* d_temp;
  inputHost = new T[n];
  for(size_t i=0; i<n; i++) {
    inputHost[i] = i%10;
  }
  const size_t numBytes = n * sizeof(T);

  CUDA_RT_CALL( cudaMalloc(&d_input, numBytes) );
  CUDA_RT_CALL(
      cudaMemcpy(d_input, inputHost, numBytes, cudaMemcpyHostToDevice));

  size_t temp_bytes = 840;
  CUDA_RT_CALL( cudaMalloc(&d_temp, temp_bytes) );

  cudaStream_t stream;
  cudaStreamCreate(&stream);
  nvcompCascadedFormatOpts opts;
  double est_ratio;
  bool threw_exception=false;

  nvcompCascadedSelectorOpts selector_opts;
  selector_opts.sample_size = 1024;
  selector_opts.num_samples = 1000;

  // Should throw exception if not enough temp workspace

  try {
    nvcompError_t err = nvcompCascadedSelectorSelectConfig(
           d_input,
           numBytes,
           getnvcompType<T>(),
           selector_opts,
           d_temp,
           temp_bytes,
           &opts,
           &est_ratio,
           stream);

    cudaStreamSynchronize(stream);
  }
  catch(const std::runtime_error& e ) {
    threw_exception = true;
  }
  REQUIRE(threw_exception == true);



  selector_opts.num_samples = 100;
// Should run and get a good compression ratio estimate.
  nvcompError_t err = nvcompCascadedSelectorSelectConfig(
           d_input,
           numBytes,
           getnvcompType<T>(),
           selector_opts,
           d_temp,
           temp_bytes,
           &opts,
           &est_ratio,
           stream);
  cudaStreamSynchronize(stream);

  REQUIRE(opts.num_RLEs == 2);
  REQUIRE(opts.num_deltas == 1);
  REQUIRE(opts.use_bp == 1);
  REQUIRE(est_ratio > 10.0);

}
