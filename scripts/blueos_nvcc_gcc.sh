#!/usr/bin/env bash

###############################################################################
# Copyright (c) 2017-24, Lawrence Livermore National Security, LLC
# and RAJA project contributors. See the RAJAPerf/LICENSE file for details.
#
# SPDX-License-Identifier: (BSD-3-Clause)
###############################################################################


BUILD_SUFFIX=cuda_12.1.1
RAJA_HOSTCONFIG=../tpl/RAJA/host-configs/lc-builds/blueos/nvcc_gcc_X.cmake

echo
echo "Creating build directory build_${BUILD_SUFFIX} and generating configuration in it"
echo "Configuration extra arguments:"
echo "   $@"
echo

rm -rf build_${BUILD_SUFFIX} >/dev/null
mkdir build_${BUILD_SUFFIX} && cd build_${BUILD_SUFFIX}

CUDA="/software/slurm/spackages/linux-rocky8-x86_64/gcc-12.2.0/cuda-12.1.1-mk44ku5ql2hnzo7lpuxxcf2wlbowunif"
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++ \
  -DBLT_CXX_STD=c++14 \
  -C ${RAJA_HOSTCONFIG} \
  -DENABLE_OPENMP=On \
  -DENABLE_CUDA=On \
  -DCUDA_SEPARABLE_COMPILATION=On \
  -DCUDA_TOOLKIT_ROOT_DIR=$CUDA \
  -DCMAKE_CUDA_COMPILER=$CUDA/bin/nvcc \
  -DCMAKE_CUDA_ARCHITECTURES=80 \
  -DCMAKE_INSTALL_PREFIX=../install_${BUILD_SUFFIX} \
  "$@" \
  ..

echo
echo "***********************************************************************"
echo
echo "cd into directory build_${BUILD_SUFFIX} and run make to build RAJA Perf Suite"
echo
echo "  Please note that you have to disable CUDA GPU hooks when you run"
echo "  the RAJA Perf Suite; for example,"
echo
echo "    lrun -1 --smpiargs="-disable_gpu_hooks" ./bin/raja-perf.exe"
echo
echo "***********************************************************************"
