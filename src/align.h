#pragma once

#define T_SIZE 32 // Size of a tile in the bayer mosaiced image
#define T_SIZE_2                                                               \
  16 // Half of T_SIZE and the size of a tile throughout the alignment pyramid

#define MIN_OFFSET \
  -680 // Min total alignment (expanded from -168 to accommodate gyro prior offsets)
#define MAX_OFFSET \
  512 // Max total alignment (expanded from 126 to accommodate gyro prior offsets)

#define DOWNSAMPLE_RATE                                                        \
  4 // Rate at which layers of the alignment pyramid are downsampled relative to
    // each other

#include "Halide.h"

/*
 * prev_tile -- Returns an index to the nearest tile in the previous level of
 * the pyramid.
 */
inline Halide::Expr prev_tile(Halide::Expr t) {
  return (t - 1) / DOWNSAMPLE_RATE;
}

/*
 * tile_0 -- Returns the upper (for y input) or left (for x input) tile that an
 * image index touches.
 */
inline Halide::Expr tile_0(Halide::Expr e) { return e / T_SIZE_2 - 1; }

/*
 * tile_1 -- Returns the lower (for y input) or right (for x input) tile that an
 * image index touches.
 */
inline Halide::Expr tile_1(Halide::Expr e) { return e / T_SIZE_2; }

/*
 * idx_0 -- Returns the inner index into the upper (for y input) or left (for x
 * input) tile that an image index touches.
 */
inline Halide::Expr idx_0(Halide::Expr e) { return e % T_SIZE_2 + T_SIZE_2; }

/*
 * idx_1 -- Returns the inner index into the lower (for y input) or right (for x
 * input) tile that an image index touches.
 */
inline Halide::Expr idx_1(Halide::Expr e) { return e % T_SIZE_2; }

/*
 * idx_im -- Returns the image index given a tile and the inner index into the
 * tile.
 */
inline Halide::Expr idx_im(Halide::Expr t, Halide::Expr i) {
  return t * T_SIZE_2 + i;
}

/*
 * idx_layer -- Returns the image index given a tile and the inner index into
 * the tile.
 */
inline Halide::Expr idx_layer(Halide::Expr t, Halide::Expr i) {
  return t * T_SIZE_2 / 2 + i;
}

/*
 * align -- Aligns multiple raw RGGB frames of a scene in T_SIZE x T_SIZE tiles
 * which overlap by T_SIZE_2 in each dimension. align(imgs)(tile_x, tile_y, n)
 * is a point representing the x and y offset for a tile in layer n that most
 * closely matches that tile in the reference (relative to the reference tile's
 * location).
 * Optionally supports gyro prior displacements (gyro_x(n), gyro_y(n)) at the coarsest layer.
 */
Halide::Func align(Halide::Buffer<uint16_t> imgs);
Halide::Func align(const Halide::Func imgs, Halide::Expr width,
                   Halide::Expr height);
Halide::Func align(const Halide::Func imgs, Halide::Expr width,
                   Halide::Expr height,
                   Halide::Func gyro_offsets);
Halide::Func align(const Halide::Func imgs, Halide::Expr width,
                   Halide::Expr height,
                   Halide::Func gyro_x, Halide::Func gyro_y);
