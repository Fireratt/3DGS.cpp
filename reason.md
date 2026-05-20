# Rendering Regression Analysis (May 2026)

## 1) Image upside down

### Symptom
The rendered image appears vertically flipped (upside down) after the projection/camera changes.

### Root cause
This pipeline already converts NDC to pixel coordinates in the compute shader (`ndc2Pix()` in `src/shaders/preprocess.comp`) assuming a **+Y up** NDC convention. When I also applied the typical Vulkan “Y flip” (`P[1][1] *= -1`) in `Renderer::updateUniforms()`, the result became an extra flip, producing an upside-down image.

### Fix
Remove the extra projection Y-flip in `src/Renderer.cpp` so the existing NDC→pixel mapping remains consistent.

## 2) Abnormal Gaussian artifacts

### Symptom
Occasional “abnormal” splats/blobs (looks like a few Gaussians exploding or producing incorrect conics/radii), visible as artifacts.

### Likely cause
The 2D covariance inversion path is sensitive to near-singular covariances:

- `compute_cov2d()` builds the projected covariance, then `main()` in `preprocess.comp` inverts it (`conic = inverse(cov2d)`).
- If `det(cov2d)` is **positive but extremely small**, `inverse()` produces very large values.
- That can yield extremely sharp/high-energy splats or unstable radii/conic coefficients that show up as isolated artifacts.

This became more likely after reducing the diagonal regularization term from `0.3` to `0.15`, which made the covariance matrix less well-conditioned.

### Fix
- Increase the diagonal regularization to a safer value (`0.25f`) to keep covariances well-conditioned.
- Reject near-singular covariances by tightening the determinant check from `det <= 0` to `det <= 1e-6` before inverting.

These changes reduce the probability of unstable conics while keeping most of the sharpness gain versus the original `0.3f`.

从数学上说，这个现象本质是：

投影后的 Gaussian covariance condition number 过高，矩阵求逆将微小误差放大，最终导致屏幕空间高斯参数失真。

在 3DGS 论文和很多实现里，这类问题通常叫：

numerical instability in covariance inversion