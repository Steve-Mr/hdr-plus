#pragma once

#include "Halide.h"

/*
 * merge_temporal_weights -- computes temporal motion weights for tiles of aligned frames,
 * using dynamic shot noise thresholding and 3x3 DilateMask morphological ghost rejection.
 */
Halide::Func merge_temporal_weights(Halide::Func imgs, Halide::Expr width,
                                    Halide::Expr height, Halide::Func alignment);

/*
 * merge -- fully merges aligned frames in the temporal and spatial
 * dimension to produce one denoised bayer frame.
 */
Halide::Func merge(Halide::Func imgs, Halide::Expr width, Halide::Expr height,
                   Halide::Expr frames, Halide::Func alignment);
Halide::Func merge(Halide::Buffer<uint16_t> imgs, Halide::Func alignment);
