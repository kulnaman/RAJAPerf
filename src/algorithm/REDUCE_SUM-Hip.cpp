//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2017-23, Lawrence Livermore National Security, LLC
// and RAJA Performance Suite project contributors.
// See the RAJAPerf/LICENSE file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

#include "REDUCE_SUM.hpp"

#include "RAJA/RAJA.hpp"

#if defined(RAJA_ENABLE_HIP)

#if defined(__HIPCC__)
#define ROCPRIM_HIP_API 1
#include "rocprim/device/device_reduce.hpp"
#elif defined(__CUDACC__)
#include "cub/device/device_reduce.cuh"
#include "cub/util_allocator.cuh"
#endif

#include "common/HipDataUtils.hpp"

#include <iostream>
#include <utility>


namespace rajaperf
{
namespace algorithm
{

template < size_t block_size >
__launch_bounds__(block_size)
__global__ void reduce_sum(Real_ptr x, Real_ptr dsum, Real_type sum_init,
                           Index_type iend)
{
  HIP_DYNAMIC_SHARED(Real_type, psum);

  Index_type i = blockIdx.x * block_size + threadIdx.x;

  psum[ threadIdx.x ] = sum_init;
  for ( ; i < iend ; i += gridDim.x * block_size ) {
    psum[ threadIdx.x ] += x[i];
  }
  __syncthreads();

  for ( i = block_size / 2; i > 0; i /= 2 ) {
    if ( threadIdx.x < i ) {
      psum[ threadIdx.x ] += psum[ threadIdx.x + i ];
    }
     __syncthreads();
  }

  if ( threadIdx.x == 0 ) {
    RAJA::atomicAdd<RAJA::hip_atomic>( dsum, psum[ 0 ] );
  }
}


void REDUCE_SUM::runHipVariantRocprim(VariantID vid)
{
  const Index_type run_reps = getRunReps();
  const Index_type ibegin = 0;
  const Index_type iend = getActualProblemSize();

  auto res{getHipResource()};

  REDUCE_SUM_DATA_SETUP;

  if ( vid == Base_HIP ) {

    hipStream_t stream = res.get_stream();

    int len = iend - ibegin;

    DataSpace rds = getReductionDataSpace(vid);
    DataSpace hrds = hostAccessibleDataSpace(rds);
    const bool separate_buffers = hrds != rds;

    Real_ptr dsum;
    allocData(rds, dsum, 1);
    Real_ptr hsum = dsum;
    if (separate_buffers) {
      allocData(hrds, hsum, 1);
    }

    // Determine temporary device storage requirements
    void* d_temp_storage = nullptr;
    size_t temp_storage_bytes = 0;
#if defined(__HIPCC__)
    hipErrchk(::rocprim::reduce(d_temp_storage,
                                temp_storage_bytes,
                                x+ibegin,
                                dsum,
                                m_sum_init,
                                len,
                                rocprim::plus<Real_type>(),
                                stream));
#elif defined(__CUDACC__)
    hipErrchk(::cub::DeviceReduce::Reduce(d_temp_storage,
                                          temp_storage_bytes,
                                          x+ibegin,
                                          dsum,
                                          len,
                                          ::cub::Sum(),
                                          m_sum_init,
                                          stream));
#endif

    // Allocate temporary storage
    unsigned char* temp_storage;
    allocData(DataSpace::HipDevice, temp_storage, temp_storage_bytes);
    d_temp_storage = temp_storage;


    startTimer();
    for (RepIndex_type irep = 0; irep < run_reps; ++irep) {

      // Run
#if defined(__HIPCC__)
      hipErrchk(::rocprim::reduce(d_temp_storage,
                                  temp_storage_bytes,
                                  x+ibegin,
                                  dsum,
                                  m_sum_init,
                                  len,
                                  rocprim::plus<Real_type>(),
                                  stream));
#elif defined(__CUDACC__)
      hipErrchk(::cub::DeviceReduce::Reduce(d_temp_storage,
                                            temp_storage_bytes,
                                            x+ibegin,
                                            dsum,
                                            len,
                                            ::cub::Sum(),
                                            m_sum_init,
                                            stream));
#endif

      if (separate_buffers) {
        hipErrchk( hipMemcpyAsync( hsum, dsum, sizeof(Real_type),
                                   hipMemcpyDeviceToHost, stream ) );
      }

      hipErrchk(hipStreamSynchronize(stream));
      m_sum = *hsum;

    }
    stopTimer();

    // Free temporary storage
    deallocData(DataSpace::HipDevice, temp_storage);
    deallocData(rds, dsum);
    if (separate_buffers) {
      deallocData(hrds, hsum);
    }

  } else {

    getCout() << "\n  REDUCE_SUM : Unknown Hip variant id = " << vid << std::endl;

  }

}

template < size_t block_size >
void REDUCE_SUM::runHipVariantBlock(VariantID vid)
{
  const Index_type run_reps = getRunReps();
  const Index_type ibegin = 0;
  const Index_type iend = getActualProblemSize();

  auto res{getHipResource()};

  REDUCE_SUM_DATA_SETUP;

  if ( vid == Base_HIP ) {

    DataSpace rds = getReductionDataSpace(vid);
    DataSpace hrds = hostAccessibleDataSpace(rds);
    const bool separate_buffers = hrds != rds;

    Real_ptr dsum;
    allocData(rds, dsum, 1);
    Real_ptr hsum = dsum;
    if (separate_buffers) {
      allocData(hrds, hsum, 1);
    }

    startTimer();
    for (RepIndex_type irep = 0; irep < run_reps; ++irep) {

      if (separate_buffers) {
        *hsum = m_sum_init;
        hipErrchk( hipMemcpyAsync( dsum, hsum, sizeof(Real_type),
                                   hipMemcpyHostToDevice, res.get_stream() ) );
      } else {
        *dsum = m_sum_init;
      }

      const size_t grid_size = RAJA_DIVIDE_CEILING_INT(iend, block_size);
      constexpr size_t shmem = sizeof(Real_type)*block_size;
      hipLaunchKernelGGL( (reduce_sum<block_size>), dim3(grid_size), dim3(block_size),
                          shmem, res.get_stream(),
                          x, dsum, m_sum_init, iend );
      hipErrchk( hipGetLastError() );

      if (separate_buffers) {
        hipErrchk( hipMemcpyAsync( hsum, dsum, sizeof(Real_type),
                                   hipMemcpyDeviceToHost, res.get_stream() ) );
      }
      hipErrchk( hipStreamSynchronize( res.get_stream() ) );
      m_sum = *hsum;

    }
    stopTimer();

    deallocData(rds, dsum);
    if (separate_buffers) {
      deallocData(hrds, hsum);
    }

  } else if ( vid == RAJA_HIP ) {

    startTimer();
    for (RepIndex_type irep = 0; irep < run_reps; ++irep) {

      RAJA::ReduceSum<RAJA::hip_reduce, Real_type> sum(m_sum_init);

      RAJA::forall< RAJA::hip_exec<block_size, true /*async*/> >( res,
        RAJA::RangeSegment(ibegin, iend), [=] __device__ (Index_type i) {
          REDUCE_SUM_BODY;
      });

      m_sum = sum.get();

    }
    stopTimer();

  } else {

    getCout() << "\n  REDUCE_SUM : Unknown Hip variant id = " << vid << std::endl;

  }

}

template < size_t block_size >
void REDUCE_SUM::runHipVariantOccGS(VariantID vid)
{
  const Index_type run_reps = getRunReps();
  const Index_type ibegin = 0;
  const Index_type iend = getActualProblemSize();

  auto res{getHipResource()};

  REDUCE_SUM_DATA_SETUP;

  if ( vid == Base_HIP ) {

    DataSpace rds = getReductionDataSpace(vid);
    DataSpace hrds = hostAccessibleDataSpace(rds);
    const bool separate_buffers = hrds != rds;

    Real_ptr dsum;
    allocData(rds, dsum, 1);
    Real_ptr hsum = dsum;
    if (separate_buffers) {
      allocData(hrds, hsum, 1);
    }

    constexpr size_t shmem = sizeof(Real_type)*block_size;
    const size_t max_grid_size = detail::getHipOccupancyMaxBlocks(
        (reduce_sum<block_size>), block_size, shmem);

    startTimer();
    for (RepIndex_type irep = 0; irep < run_reps; ++irep) {

      if (separate_buffers) {
        *hsum = m_sum_init;
        hipErrchk( hipMemcpyAsync( dsum, hsum, sizeof(Real_type),
                                   hipMemcpyHostToDevice, res.get_stream() ) );
      } else {
        *dsum = m_sum_init;
      }

      const size_t normal_grid_size = RAJA_DIVIDE_CEILING_INT(iend, block_size);
      const size_t grid_size = std::min(normal_grid_size, max_grid_size);
      hipLaunchKernelGGL( (reduce_sum<block_size>), dim3(grid_size), dim3(block_size),
                          shmem, res.get_stream(),
                          x, dsum, m_sum_init, iend );
      hipErrchk( hipGetLastError() );

      if (separate_buffers) {
        hipErrchk( hipMemcpyAsync( hsum, dsum, sizeof(Real_type),
                                   hipMemcpyDeviceToHost, res.get_stream() ) );
      }
      hipErrchk( hipStreamSynchronize( res.get_stream() ) );
      m_sum = *hsum;

    }
    stopTimer();

    deallocData(rds, dsum);
    if (separate_buffers) {
      deallocData(hrds, hsum);
    }

  } else if ( vid == RAJA_HIP ) {

    startTimer();
    for (RepIndex_type irep = 0; irep < run_reps; ++irep) {

      RAJA::ReduceSum<RAJA::hip_reduce, Real_type> sum(m_sum_init);

      RAJA::forall< RAJA::hip_exec_occ_calc<block_size, true /*async*/> >( res,
        RAJA::RangeSegment(ibegin, iend), [=] __device__ (Index_type i) {
          REDUCE_SUM_BODY;
      });

      m_sum = sum.get();

    }
    stopTimer();

  } else {

    getCout() << "\n  REDUCE_SUM : Unknown Hip variant id = " << vid << std::endl;

  }

}

void REDUCE_SUM::runHipVariant(VariantID vid, size_t tune_idx)
{
  size_t t = 0;

  if ( vid == Base_HIP ) {

    if (tune_idx == t) {

      runHipVariantRocprim(vid);

    }

    t += 1;

  }

  if ( vid == Base_HIP || vid == RAJA_HIP ) {

    seq_for(gpu_block_sizes_type{}, [&](auto block_size) {

      if (run_params.numValidGPUBlockSize() == 0u ||
          run_params.validGPUBlockSize(block_size)) {

        if (tune_idx == t) {

          setBlockSize(block_size);
          runHipVariantBlock<block_size>(vid);

        }

        t += 1;

        if (tune_idx == t) {

          setBlockSize(block_size);
          runHipVariantOccGS<block_size>(vid);

        }

        t += 1;

      }

    });

  } else {

    getCout() << "\n  REDUCE_SUM : Unknown Hip variant id = " << vid << std::endl;

  }

}

void REDUCE_SUM::setHipTuningDefinitions(VariantID vid)
{
  if ( vid == Base_HIP ) {

#if defined(__HIPCC__)
    addVariantTuningName(vid, "rocprim");
#elif defined(__CUDACC__)
    addVariantTuningName(vid, "cub");
#endif

  }

  if ( vid == Base_HIP || vid == RAJA_HIP ) {

    seq_for(gpu_block_sizes_type{}, [&](auto block_size) {

      if (run_params.numValidGPUBlockSize() == 0u ||
          run_params.validGPUBlockSize(block_size)) {

        addVariantTuningName(vid, "block_"+std::to_string(block_size));

        addVariantTuningName(vid, "occgs_"+std::to_string(block_size));

      }

    });

  }

}

} // end namespace algorithm
} // end namespace rajaperf

#endif  // RAJA_ENABLE_HIP
