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
/*
 * merge_temporal_weights -- computes temporal motion weights for tiles of aligned frames,
 * using dynamic shot noise thresholding and 3x3 DilateMask morphological ghost rejection.
 */
Func merge_temporal_weights(Halide::Func imgs, Expr width, Expr height,
                            Func alignment) {

  Func weight("merge_temporal_weights");

  Var tx, ty, n;
  RDom r0(0, 16, 0, 16); // reduction over pixels in downsampled tile
  RDom r_ref(0, 16, 0, 16); // reduction over reference tile for smoothness

  // mirror input with overlapping edges
  Func imgs_mirror = BoundaryConditions::mirror_interior(
      imgs, {Range(0, width), Range(0, height)});

  // downsampled layer for computing L1 distances
  Func layer = box_down2(imgs_mirror, "merge_layer");

  // constants for determining strength and robustness of temporal merge
  float factor = 8.f; // factor by which inverse function is elongated
  int min_dist = 10;  // pixel L1 distance below which weight is maximal
  int max_dist = 300; // pixel L1 distance above which weight is zero

  // Reference tile smoothness analysis:
  // Measure local contrast / spatial gradient of the reference frame across the tile.
  // In a smooth/flat region (e.g. ceramic mug, uniform sky, flat wall), ref_variation is small
  // and dominated solely by shot noise. True ghosting cannot visually occur on flat regions.
  Expr ref_x_smooth = idx_layer(tx, r_ref.x);
  Expr ref_y_smooth = idx_layer(ty, r_ref.y);
  Expr ref_val_smooth = layer(ref_x_smooth, ref_y_smooth, 0);
  Expr ref_mean_smooth = sum(i32(ref_val_smooth)) / 256;
  Expr ref_grad_x = abs(i32(layer(ref_x_smooth + 1, ref_y_smooth, 0)) - i32(ref_val_smooth));
  Expr ref_grad_y = abs(i32(layer(ref_x_smooth, ref_y_smooth + 1, 0)) - i32(ref_val_smooth));
  Expr ref_variation = sum(ref_grad_x + ref_grad_y) / 256;

  Expr noise_floor_smooth = min_dist + cast<int32_t>(sqrt(max(0.0f, f32(ref_mean_smooth) * 2.0f)));
  Expr var_threshold = max(60.0f, f32(noise_floor_smooth) * 2.5f);

  Func smoothness("merge_temporal_smoothness");
  smoothness(tx, ty) = clamp(1.0f - f32(ref_variation) / var_threshold, 0.0f, 1.0f);

  Point offset = clamp(P(alignment(tx, ty, n)), P(MIN_OFFSET, MIN_OFFSET),
                       P(MAX_OFFSET, MAX_OFFSET));

  Expr al_x = idx_layer(tx, r0.x) + offset.x / 2;
  Expr al_y = idx_layer(ty, r0.y) + offset.y / 2;

  Expr ref_val = layer(idx_layer(tx, r0.x), idx_layer(ty, r0.y), 0);
  Expr alt_val = layer(al_x, al_y, n);

  // average L1 distance in tile
  Expr dist = sum(abs(i32(ref_val) - i32(alt_val))) / 256;

  // Dynamic noise thresholding:
  // Tile average intensity of the reference frame:
  Expr ref_mean = sum(i32(ref_val)) / 256;

  // Estimate noise floor based on signal brightness (Poisson shot noise variance ~ signal):
  // In dark areas, noise floor stays at baseline min_dist (10).
  // In bright / noisy areas, noise floor expands smoothly to prevent photon noise from being misclassified as motion.
  Expr noise_floor = min_dist + cast<int32_t>(sqrt(max(0.0f, f32(ref_mean) * 2.0f)));
  Expr base_min_dist = max(min_dist, noise_floor);
  Expr base_max_dist = max(max_dist, base_min_dist * 8);

  // Smoothness adaptation: expand thresholds on smooth tiles to prevent false motion triggers on subtle gradients
  Expr smooth_factor = smoothness(tx, ty);
  Expr dynamic_min_dist = base_min_dist + cast<int32_t>(smooth_factor * f32(base_min_dist) * 1.5f);
  Expr dynamic_max_dist = base_max_dist + cast<int32_t>(smooth_factor * f32(base_max_dist));
  Expr norm_dist = max(1, (i32(dist) - dynamic_min_dist) / factor);

  // Soft cutoff transition:
  // Instead of a hard cliff dropping abruptly to 0.f at dynamic_max_dist,
  // apply a smooth Hermite decay between dynamic_max_dist and dynamic_cutoff_dist.
  Expr dist_f = f32(dist);
  Expr d_max_f = f32(dynamic_max_dist);
  Expr d_cutoff_f = d_max_f * 1.5f;

  Expr t = clamp((d_cutoff_f - dist_f) / max(1.0f, d_cutoff_f - d_max_f), 0.0f, 1.0f);
  Expr soft_decay = t * t * (3.0f - 2.0f * t);
  Expr base_weight = 1.f / f32(norm_dist);

  // Raw weight for each tile in temporal merge:
  // Below dynamic_max_dist: base_weight
  // Between dynamic_max_dist and dynamic_cutoff_dist: continuous soft decay
  // Above dynamic_cutoff_dist: strictly 0.f (motion rejection)
  Func raw_weight("merge_temporal_raw_weights");
  raw_weight(tx, ty, n) = select(dist_f >= d_cutoff_f, 0.f,
                                 select(dist_f > d_max_f, base_weight * soft_decay,
                                        base_weight));

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
  //    smoothly attenuate weight based on neighbor motion density.
  //    IMPORTANT: On smooth/flat tiles (smooth_factor > 0), neighbor motion cannot produce visual ghosting
  //    as there are no high-contrast edges to bleed across tiles. Modulating attenuation by (1.0f - smooth_factor)
  //    prevents smooth tiles from being needlessly penalized and creating tile seam / checkerboard artifacts.
  // 3. Otherwise (clean static neighborhood), use full raw_weight.
  Expr motion_atten = clamp(1.f - neighbor_motion_sum * 0.25f, 0.f, 1.f);
  Expr effective_atten = motion_atten * (1.0f - smooth_factor) + 1.0f * smooth_factor;

  weight(tx, ty, n) = select(m11 > 0.5f, 0.f,
                             select(dilated_max > 0.5f,
                                    raw_weight(tx, ty, n) * effective_atten,
                                    raw_weight(tx, ty, n)));

  ///////////////////////////////////////////////////////////////////////////
  // schedule
  ///////////////////////////////////////////////////////////////////////////

  smoothness.compute_root().parallel(ty).vectorize(tx, 16);
  raw_weight.compute_root().parallel(ty).vectorize(tx, 16);
  motion_mask.compute_root().parallel(ty).vectorize(tx, 16);
  weight.compute_root().parallel(ty).vectorize(tx, 16);

  return weight;
}

Func merge_temporal(Halide::Func imgs, Expr width, Expr height, Expr frames,
                    Func alignment) {

  Func weight = merge_temporal_weights(imgs, width, height, alignment);
  Func total_weight("merge_temporal_total_weights");
  Func output("merge_temporal_output");

  Var ix, iy, tx, ty;
  RDom r1(1, frames - 1); // reduction over alternate images

  Func imgs_mirror = BoundaryConditions::mirror_interior(
      imgs, {Range(0, width), Range(0, height)});

  // total weight for each tile in a temporal stack of images
  total_weight(tx, ty) = sum(weight(tx, ty, r1)) +
                         1.f; // additional 1.f accounting for reference image

  // expressions for summing over images at each pixel
  Point offset = P(alignment(tx, ty, r1));

  Expr al_x = idx_im(tx, ix) + offset.x;
  Expr al_y = idx_im(ty, iy) + offset.y;

  Expr ref_val = imgs_mirror(idx_im(tx, ix), idx_im(ty, iy), 0);
  Expr alt_val = imgs_mirror(al_x, al_y, r1);

  // temporal merge function using weighted pixel values
  output(ix, iy, tx, ty) =
      sum(weight(tx, ty, r1) * alt_val / total_weight(tx, ty)) +
      ref_val / total_weight(tx, ty);

  ///////////////////////////////////////////////////////////////////////////
  // schedule
  ///////////////////////////////////////////////////////////////////////////

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
