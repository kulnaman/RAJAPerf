//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//
// Copyright (c) 2017-20, Lawrence Livermore National Security, LLC
// and RAJA Performance Suite project contributors.
// See the RAJAPerf/COPYRIGHT file for details.
//
// SPDX-License-Identifier: (BSD-3-Clause)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~//

#include "MAT_MAT_SHARED.hpp"

#include "RAJA/RAJA.hpp"

#if defined(RAJA_ENABLE_HIP)

#include "common/HipDataUtils.hpp"

#include <iostream>

namespace rajaperf {
namespace basic {

#define MAT_MAT_SHARED_DATA_SETUP_HIP                                          \
  const Index_type NN = m_N * m_N;                                             \
  allocAndInitHipDeviceData(A, m_A, NN);                                       \
  allocAndInitHipDeviceData(B, m_B, NN);                                       \
  allocAndInitHipDeviceData(C, m_C, NN);

#define MAT_MAT_SHARED_DATA_TEARDOWN_HIP                                       \
  getHipDeviceData(m_A, A, NN);                                                \
  getHipDeviceData(m_B, B, NN);                                                \
  getHipDeviceData(m_C, C, NN);                                                \
  deallocHipDeviceData(A);                                                     \
  deallocHipDeviceData(B);                                                     \
  deallocHipDeviceData(C);

__global__ void mat_mat_shared(Index_type N, Real_ptr C, Real_ptr A,
                               Real_ptr B) {

  Index_type tx = threadIdx.x;
  Index_type ty = threadIdx.y;
  Index_type bx = blockIdx.x;
  Index_type by = blockIdx.y;

  MAT_MAT_SHARED_BODY_0

  MAT_MAT_SHARED_BODY_1

  for (Index_type k = 0; k < (TL_SZ + N - 1) / TL_SZ; k++) {

    MAT_MAT_SHARED_BODY_2

    __syncthreads();

    MAT_MAT_SHARED_BODY_3

    __syncthreads();
  }

  MAT_MAT_SHARED_BODY_4
}

void MAT_MAT_SHARED::runHipVariant(VariantID vid) {

  const Index_type run_reps = getRunReps();
  const Index_type N = m_N;

  dim3 block_size(TL_SZ, TL_SZ);
  dim3 grid_size(RAJA_DIVIDE_CEILING_INT(N, block_size.x),
               RAJA_DIVIDE_CEILING_INT(N, block_size.y));

  const Index_type Nx = grid_size.x;
  const Index_type Ny = grid_size.y;

  MAT_MAT_SHARED_DATA_SETUP;

  if (vid == Base_HIP) {

    MAT_MAT_SHARED_DATA_SETUP_HIP;

    startTimer();
    for (RepIndex_type irep = 0; irep < run_reps; ++irep) {

      hipLaunchKernelGGL((mat_mat_shared), dim3(grid_size), dim3(block_size), 0, 0,
                         N, A, B, C);

      hipErrchk( hipGetLastError() );
    }
    stopTimer();

    MAT_MAT_SHARED_DATA_TEARDOWN_HIP;

  } else if (vid == Lambda_HIP) {

    MAT_MAT_SHARED_DATA_SETUP_HIP;

    startTimer();
    for (RepIndex_type irep = 0; irep < run_reps; ++irep) {

      auto mat_mat_shared_lam = [=] __device__() {

        auto outer_y = [&](Index_type by) {
          auto outer_x = [&](Index_type bx) {
            MAT_MAT_SHARED_BODY_0

            auto inner_y_1 = [&](Index_type ty) {
              auto inner_x_1 = [&](Index_type tx) { MAT_MAT_SHARED_BODY_1 };

              {
                Index_type tx = threadIdx.x;
                if (tx < TL_SZ)
                  inner_x_1(tx);
              }
            };

            {
              Index_type ty = threadIdx.y;
              if (ty < TL_SZ)
                inner_y_1(ty);
            }

            for (Index_type k = 0; k < (TL_SZ + N - 1) / TL_SZ; ++k) {

              auto inner_y_2 = [&](Index_type ty) {
                auto inner_x_2 = [&](Index_type tx) { MAT_MAT_SHARED_BODY_2 };

                {
                  Index_type tx = threadIdx.x;
                  if (tx < TL_SZ)
                    inner_x_2(tx);
                }
              };

              {
                Index_type ty = threadIdx.y;
                if (ty < TL_SZ)
                  inner_y_2(ty);
              }

              __syncthreads();

              auto inner_y_3 = [&](Index_type ty) {
                auto inner_x_3 = [&](Index_type tx) { MAT_MAT_SHARED_BODY_3 };

                {
                  Index_type tx = threadIdx.x;
                  if (tx < TL_SZ)
                    inner_x_3(tx);
                }
              };

              {
                Index_type ty = threadIdx.y;
                if (ty < TL_SZ)
                  inner_y_3(ty);
              }

              __syncthreads();
            }

            auto inner_y_4 = [&](Index_type ty) {
              auto inner_x_4 = [&](Index_type tx) { MAT_MAT_SHARED_BODY_4 };

              {
                Index_type tx = threadIdx.x;
                if (tx < TL_SZ)
                  inner_x_4(tx);
              }
            };

            {
              Index_type ty = threadIdx.y;
              if (ty < TL_SZ)
                inner_y_4(ty);
            }
          }; // outer_x

          {
            Index_type bx = blockIdx.x;
            if(bx < Nx) outer_x(bx);
          }
        };

        {
          Index_type by = blockIdx.y;
          if(by < Ny) outer_y(by);
        }
      };

      hipLaunchKernelGGL(lambda_hip<decltype(mat_mat_shared_lam)>,
        grid_size, block_size, 0, 0, mat_mat_shared_lam);

      hipErrchk( hipGetLastError() );
    }
    stopTimer();

    MAT_MAT_SHARED_DATA_TEARDOWN_HIP;

  } else if (vid == RAJA_HIP) {

    MAT_MAT_SHARED_DATA_SETUP_HIP;

    using launch_policy = RAJA::expt::LaunchPolicy<RAJA::expt::seq_launch_t
                                                   ,RAJA::expt::hip_launch_t<true>
                                                   >;

    using teams_x = RAJA::expt::LoopPolicy<RAJA::loop_exec
                                           ,RAJA::hip_block_x_direct
                                           >;

    using teams_y = RAJA::expt::LoopPolicy<RAJA::loop_exec
                                           ,RAJA::hip_block_y_direct
                                           >;

    using threads_x = RAJA::expt::LoopPolicy<RAJA::loop_exec
                                             ,RAJA::hip_thread_x_direct
                                             >;

    using threads_y = RAJA::expt::LoopPolicy<RAJA::loop_exec
                                             ,RAJA::hip_thread_y_direct
                                             >;

    startTimer();
    for (RepIndex_type irep = 0; irep < run_reps; ++irep) {

      RAJA::expt::launch<launch_policy>(
          RAJA::expt::DEVICE,
          RAJA::expt::Resources(RAJA::expt::Teams(Nx, Ny),
                                RAJA::expt::Threads(TL_SZ, TL_SZ)),
          [=] RAJA_HOST_DEVICE(RAJA::expt::LaunchContext ctx) {

            RAJA::expt::loop<teams_y>(ctx, RAJA::RangeSegment(0, Ny), [&](Index_type by) {
               RAJA::expt::loop<teams_x>(ctx, RAJA::RangeSegment(0, Nx), [&](Index_type bx) {

                   MAT_MAT_SHARED_BODY_0

                   RAJA::expt::loop<threads_y>(ctx, RAJA::RangeSegment(0, TL_SZ), [&](Index_type ty) {
                     RAJA::expt::loop<threads_x>(ctx, RAJA::RangeSegment(0, TL_SZ), [&](Index_type tx) {
                         MAT_MAT_SHARED_BODY_1
                     });
                   });

                   for (Index_type k = 0; k < (TL_SZ + N - 1) / TL_SZ; k++) {

                     RAJA::expt::loop<threads_y>(ctx, RAJA::RangeSegment(0, TL_SZ), [&](Index_type ty) {
                       RAJA::expt::loop<threads_x>(ctx, RAJA::RangeSegment(0, TL_SZ), [&](Index_type tx) {
                           MAT_MAT_SHARED_BODY_2
                        });
                      });

                      ctx.teamSync();

                      RAJA::expt::loop<threads_y>(ctx, RAJA::RangeSegment(0, TL_SZ), [&](Index_type ty) {
                        RAJA::expt::loop<threads_x>(ctx, RAJA::RangeSegment(0, TL_SZ), [&](Index_type tx) {
                            MAT_MAT_SHARED_BODY_3
                        });
                      });

                      ctx.teamSync();
                    }

                    RAJA::expt::loop<threads_y>(ctx, RAJA::RangeSegment(0, TL_SZ), [&](Index_type ty) {
                      RAJA::expt::loop<threads_x>(ctx, RAJA::RangeSegment(0, TL_SZ), [&](Index_type tx) {
                          MAT_MAT_SHARED_BODY_4
                      });
                    });
               });
            });
          }); // kernel
    }
    stopTimer();

    MAT_MAT_SHARED_DATA_TEARDOWN_HIP;

  } else {
    std::cout << "\n  MAT_MAT_SHARED : Unknown Hip variant id = " << vid
              << std::endl;
  }
}

} // end namespace basic
} // end namespace rajaperf

#endif // RAJA_ENABLE_HIP