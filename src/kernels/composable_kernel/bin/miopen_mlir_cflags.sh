#!/bin/bash

# syntax
# miopen_mlir_cflags filename (-xdlops)
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
  TRANSLATE_CMD=-mlir-to-miopen-cflags-xdlops
  shift
else
  # non-XDLOPS route.
  TRANSLATE_CMD=-mlir-to-miopen-cflags
fi

# generate cflags.
${MLIR_BIN_DIR}/mlir-miopen-driver $* | ${MLIR_BIN_DIR}/mlir-opt -miopen-lowering | ${MLIR_BIN_DIR}/mlir-translate ${TRANSLATE_CMD} -o ${OUTPUT_FILE}
