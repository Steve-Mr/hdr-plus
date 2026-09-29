#include "merge.h"

#include "Halide.h"
#include "Point.h"
#include "align.h"
#include "util.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

/*
 * merge_temporal -- combines aligned tiles in the temporal dimension by
 * weighting various frames based on their L1 distance to the reference frame's
 * tile. Thresholds L1 scores so that tiles above a certain distance are
 * completely discounted, and tiles below a certain distance are assumed to be
 * perfectly aligned.
 *
 * Enhanced with dynamic noise thresholding and DilateMask morphological ghost rejection.
 */
Func merge_temporal(Halide::Func imgs, Expr width, Expr height, Expr frames,
                    Func alignment) {

  Func weight("merge_temporal_weights");
  Func total_weight("merge_temporal_total_weights");
  Func output("merge_temporal_output");

  Var ix, iy, tx, ty, n;
  RDom r0(0, 16, 0, 16);  // reduction over pixels in downsampled tile
  RDom r1(1, frames - 1); // reduction over alternate images

  // mirror input with overlapping edges

  Func imgs_mirror = BoundaryConditions::mirror_interior(
      imgs, {Range(0, width), Range(0, height)});

  // downsampled layer for computing L1 distances

  Func layer = box_down2(imgs_mirror, "merge_layer");

  // alignment offset, indicies and pixel value expressions; used twice in
  // different reductions

  Point offset;
  Expr al_x, al_y, ref_val, alt_val;

  // expressions for summing over pixels in each tile

  offset = clamp(P(alignment(tx, ty, n)), P(MIN_OFFSET, MIN_OFFSET),
                 P(MAX_OFFSET, MAX_OFFSET));

  al_x = idx_layer(tx, r0.x) + offset.x / 2;
  al_y = idx_layer(ty, r0.y) + offset.y / 2;

  ref_val = layer(idx_layer(tx, r0.x), idx_layer(ty, r0.y), 0);
  alt_val = layer(al_x, al_y, n);

  // constants for determining strength and robustness of temporal merge

  float factor = 8.f; // factor by which inverse function is elongated
  int min_dist = 10;  // pixel L1 distance below which weight is maximal
  int max_dist = 300; // pixel L1 distance above which weight is zero

  // average L1 distance in tile

  Expr dist = sum(abs(i32(ref_val) - i32(alt_val))) / 256;

  // Dynamic noise thresholding:
  // Tile average intensity of the reference frame:
  Expr ref_mean = sum(i32(ref_val)) / 256;

  // Estimate noise floor based on signal brightness (Poisson shot noise variance ~ signal):
  // In dark areas, noise floor stays at baseline min_dist (10).
  // In bright / noisy areas, noise floor expands smoothly to prevent photon noise from being misclassified as motion.
  Expr noise_floor = min_dist + cast<int32_t>(sqrt(max(0.0f, f32(ref_mean) * 2.0f)));
  Expr dynamic_min_dist = max(min_dist, noise_floor);
  Expr dynamic_max_dist = max(max_dist, dynamic_min_dist * 8);

  Expr norm_dist = max(1, (i32(dist) - dynamic_min_dist) / factor);
  Expr thresh = (dynamic_max_dist - dynamic_min_dist);

  // Raw weight for each tile in temporal merge; inversely proportional to L1 distance
  Func raw_weight("merge_temporal_raw_weights");
  raw_weight(tx, ty, n) =
      select(norm_dist > thresh, 0.f, 1.f / f32(norm_dist));

  // Motion mask: 1.0f where tile is classified as motion (raw_weight == 0.f), 0.0f otherwise
  Func motion_mask("merge_temporal_motion_mask");
  motion_mask(tx, ty, n) = select(raw_weight(tx, ty, n) == 0.f, 1.f, 0.f);

  // 3x3 DilateMask Morphological Ghosting Rejection:
  // Boundary-clamped sampling of the 3x3 neighborhood of motion_mask
  Expr num_tx = width / T_SIZE_2 - 1;
  Expr num_ty = height / T_SIZE_2 - 1;

  Expr tx_m = clamp(tx - 1, 0, num_tx);
  Expr tx_p = clamp(tx + 1, 0, num_tx);
  Expr ty_m = clamp(ty - 1, 0, num_ty);
  Expr ty_p = clamp(ty + 1, 0, num_ty);

  Expr m00 = motion_mask(tx_m, ty_m, n);
  Expr m01 = motion_mask(tx,   ty_m, n);
  Expr m02 = motion_mask(tx_p, ty_m, n);
  Expr m10 = motion_mask(tx_m, ty,   n);
  Expr m11 = motion_mask(tx,   ty,   n);
  Expr m12 = motion_mask(tx_p, ty,   n);
  Expr m20 = motion_mask(tx_m, ty_p, n);
  Expr m21 = motion_mask(tx,   ty_p, n);
  Expr m22 = motion_mask(tx_p, ty_p, n);

  Expr neighbor_motion_sum = m00 + m01 + m02 + m10 + m11 + m12 + m20 + m21 + m22;
  Expr dilated_max = max(max(max(m00, m01), max(m02, m10)),
                         max(max(m11, m12), max(m20, max(m21, m22))));

  // Morphological dilation ghost rejection with smooth boundary transition:
  // 1. If center tile itself has motion (m11 > 0.5f), weight is strictly 0.f (single-frame fallback).
  // 2. If center tile is static but in the 3x3 dilated motion neighborhood (dilated_max > 0.5f),
  //    smoothly attenuate weight based on neighbor motion density to eliminate seam hard cuts and purple fringing.
  // 3. Otherwise (clean static neighborhood), use full raw_weight.
  Expr motion_atten = clamp(1.f - neighbor_motion_sum * 0.25f, 0.f, 1.f);
  weight(tx, ty, n) = select(m11 > 0.5f, 0.f,
                             select(dilated_max > 0.5f,
                                    raw_weight(tx, ty, n) * motion_atten,
                                    raw_weight(tx, ty, n)));

  // total weight for each tile in a temporal stack of images

  total_weight(tx, ty) = sum(weight(tx, ty, r1)) +
                         1.f; // additional 1.f accounting for reference image

  // expressions for summing over images at each pixel

  offset = P(alignment(tx, ty, r1));

  al_x = idx_im(tx, ix) + offset.x;
  al_y = idx_im(ty, iy) + offset.y;

  ref_val = imgs_mirror(idx_im(tx, ix), idx_im(ty, iy), 0);
  alt_val = imgs_mirror(al_x, al_y, r1);

  // temporal merge function using weighted pixel values

  output(ix, iy, tx, ty) =
      sum(weight(tx, ty, r1) * alt_val / total_weight(tx, ty)) +
      ref_val / total_weight(tx, ty);

  ///////////////////////////////////////////////////////////////////////////
  // schedule
  ///////////////////////////////////////////////////////////////////////////

  raw_weight.compute_root().parallel(ty).vectorize(tx, 16);

  motion_mask.compute_root().parallel(ty).vectorize(tx, 16);

  weight.compute_root().parallel(ty).vectorize(tx, 16);

  total_weight.compute_root().parallel(ty).vectorize(tx, 16);

  output.compute_root().parallel(ty).vectorize(ix, 32);

  return output;
}

/*
 * merge_spatial -- smoothly blends between half-overlapped tiles in the spatial
 * domain using a raised cosine filter.
 */
Func merge_spatial(Func input) {

  Func weight("raised_cosine_weights");
  Func output("merge_spatial_output");

  Var v, x, y;

  // (modified) raised cosine window for determining pixel weights

  float pi = 3.141592f;
  weight(v) = 0.5f - 0.5f * cos(2 * pi * (v + 0.5f) / T_SIZE);

  // tile weights based on pixel position

  Expr weight_00 = weight(idx_0(x)) * weight(idx_0(y));
  Expr weight_10 = weight(idx_1(x)) * weight(idx_0(y));
  Expr weight_01 = weight(idx_0(x)) * weight(idx_1(y));
  Expr weight_11 = weight(idx_1(x)) * weight(idx_1(y));

  // values of pixels from each overlapping tile

  Expr val_00 = input(idx_0(x), idx_0(y), tile_0(x), tile_0(y));
  Expr val_10 = input(idx_1(x), idx_0(y), tile_1(x), tile_0(y));
  Expr val_01 = input(idx_0(x), idx_1(y), tile_0(x), tile_1(y));
  Expr val_11 = input(idx_1(x), idx_1(y), tile_1(x), tile_1(y));

  // spatial merge function using weighted pixel values

  output(x, y) = u16(weight_00 * val_00 + weight_10 * val_10 +
                     weight_01 * val_01 + weight_11 * val_11);

  ///////////////////////////////////////////////////////////////////////////
  // schedule
  ///////////////////////////////////////////////////////////////////////////

  weight.compute_root().vectorize(v, 32);

  output.compute_root().parallel(y).vectorize(x, 32);

  return output;
}

/*
 * merge -- fully merges aligned frames in the temporal and spatial
 * dimension to produce one denoised bayer frame.
 */
Func merge(Halide::Func imgs, Halide::Expr width, Halide::Expr height,
           Halide::Expr frames, Halide::Func alignment) {
  Func merge_temporal_output =
      merge_temporal(imgs, width, height, frames, alignment);
  return merge_spatial(merge_temporal_output);
}

Halide::Func merge(Halide::Buffer<uint16_t> imgs, Halide::Func alignment) {
  return merge(Halide::Func(imgs), imgs.width(), imgs.height(), imgs.extent(2),
               alignment);
}
