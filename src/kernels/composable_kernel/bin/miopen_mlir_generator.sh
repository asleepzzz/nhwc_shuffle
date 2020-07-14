#!/bin/bash

# syntax
# miopen_mlir_generator filename (-xdlops)
#                       --fil_layout filterLayout
#                       --in_layout inputLayout
#                       --out_layout outputLayout
#                       --batchszie n
#                       --in_channels c
#                       --out_channels k
#                       --in_h hi
#                       --in_w wi
#                       --out_h ho
#                       --out_w wo
#                       --fil_h y
#                       --fil_w x
#                       --dilation_h conv_dilation_h
#                       --dilation_w conv_dilation_w
#                       --conv_stride_h conv_stride_h
#                       --conv_stride_w conv_stride_w
#                       --padding_h padding_h
#                       --padding_w padding_w

# FIXME: make it configurable at CMake.
MLIR_BIN_DIR=~/llvm-project/build/bin

# Argument parsing
OUTPUT_FILE=$1
shift

if [[ $1 == "-xdlops" ]]; then
  # XDLOPS route.
  TRANSLATE_CPP_CMD=-mlir-to-miopen-cpp-xdlops
  TRANSLATE_HPP_CMD=-mlir-to-miopen-hpp-xdlops
  shift
else
  # non-XDLOPS route.
  TRANSLATE_CPP_CMD=-mlir-to-miopen-cpp
  TRANSLATE_HPP_CMD=-mlir-to-miopen-hpp
fi

# generate C++ source code.
${MLIR_BIN_DIR}/mlir-miopen-driver $* | ${MLIR_BIN_DIR}/mlir-opt -miopen-lowering | ${MLIR_BIN_DIR}/mlir-translate ${TRANSLATE_CPP_CMD} -o ${OUTPUT_FILE}.cpp

# tame C++ compiler.
echo "int main() {}" >> ${OUTPUT_FILE}.cpp

# generate C++ header.
${MLIR_BIN_DIR}/mlir-miopen-driver $* | ${MLIR_BIN_DIR}/mlir-opt -miopen-lowering | ${MLIR_BIN_DIR}/mlir-translate ${TRANSLATE_HPP_CMD} -o ${OUTPUT_FILE}.hpp
