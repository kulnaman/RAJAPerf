#!/usr/bin/env bash

###############################################################################
# Copyright (c) 2017-24, Lawrence Livermore National Security, LLC
# and RAJA project contributors. See the RAJAPerf/LICENSE file for details.
#
# SPDX-License-Identifier: (BSD-3-Clause)
###############################################################################

if [[ $# -lt 4 ]]; then
  echo
  echo "You must pass 4 arguments to the script (in this order): "
  echo "   1) compiler version number for spectrum mpi"
  echo "   2) compiler version number for nvcc (number only, not 'sm_70' for example)"
  echo "   3) CUDA compute architecture"
  echo "   4) compiler version number for gcc. "
  echo
  echo "For example: "
  echo "    blueos_spectrum_nvcc_gcc.sh rolling-release 10.2.89 70 8.3.1"
  exit
fi

COMP_MPI_VER=$1
COMP_NVCC_VER=$2
COMP_ARCH=$3
COMP_GCC_VER=$4
shift 4

BUILD_SUFFIX=lc_blueos-spectrum-${COMP_MPI_VER}-nvcc-${COMP_NVCC_VER}-${COMP_ARCH}-gcc-${COMP_GCC_VER}
RAJA_HOSTCONFIG=../tpl/RAJA/host-configs/lc-builds/blueos/nvcc_gcc_X.cmake

echo
echo "Creating build directory build_${BUILD_SUFFIX} and generating configuration in it"
echo "Configuration extra arguments:"
echo "   $@"
echo

rm -rf build_${BUILD_SUFFIX} >/dev/null
mkdir build_${BUILD_SUFFIX} && cd build_${BUILD_SUFFIX}

module load cmake/3.20.2

cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DMPI_CXX_COMPILER=/usr/tce/packages/spectrum-mpi/spectrum-mpi-${COMP_MPI_VER}-gcc-${COMP_GCC_VER}/bin/mpig++ \
  -DCMAKE_CXX_COMPILER=/usr/tce/packages/gcc/gcc-${COMP_GCC_VER}/bin/g++ \
  -DBLT_CXX_STD=c++14 \
  -C ${RAJA_HOSTCONFIG} \
  -DENABLE_MPI=On \
  -DENABLE_OPENMP=On \
  -DENABLE_CUDA=On \
  -DCUDA_SEPARABLE_COMPILATION=On \
  -DCUDA_TOOLKIT_ROOT_DIR=/usr/tce/packages/cuda/cuda-${COMP_NVCC_VER} \
  -DCMAKE_CUDA_COMPILER=/usr/tce/packages/cuda/cuda-${COMP_NVCC_VER}/bin/nvcc \
  -DCMAKE_CUDA_ARCHITECTURES=${COMP_ARCH} \
  -DCMAKE_INSTALL_PREFIX=../install_${BUILD_SUFFIX} \
  "$@" \
  ..

echo
echo "***********************************************************************"
echo
echo "cd into directory build_${BUILD_SUFFIX} and run make to build RAJA Perf Suite"
echo
echo "  Please note that you have to run with mpi when you run"
echo "  the RAJA Perf Suite; for example,"
echo
echo "    lrun -n4 ./bin/raja-perf.exe"
echo
echo "***********************************************************************"
