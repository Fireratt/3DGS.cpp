# AGENT.md

## Task: 3DGS Tile Heatmap Visualization and Low-Overhead Runtime Statistics

### Background

This repository contains a Vulkan-based 3D Gaussian Splatting renderer.

We need two debugging/profiling features:

1. **Tile Load Heatmap**

   * Visualize per-tile rendering workload as a heatmap.
   * Used for analyzing load imbalance and tile distribution.
   * Must be switchable at runtime.

2. **Per-frame Gaussian Statistics**

   * Visible Gaussian Count
   * Gaussian Instance Count (Tile Entry Count)

The implementation must introduce negligible runtime overhead and must not affect the normal rendering path when disabled.

---

# Feature 1: Tile Load Heatmap

## Goal

Display a fullscreen heatmap showing how many Gaussian instances are assigned to each tile.

The heatmap should help visualize:

* Hot tiles
* Sparse tiles
* Load imbalance
* Tile binning quality

---

## Definition

For each tile:

```text
tile_load = number of Gaussian instances inserted into this tile
```

This corresponds to the tile-entry count generated during the tile binning stage.

Do NOT use:

```text
visible pixels
rendered fragments
accumulated opacity
```

The metric must be based on tile entries.

---

## Visualization

Use a color ramp:

```text
Blue   -> low load
Green  -> medium load
Yellow -> high load
Red    -> very high load
```

Recommended normalization:

```text
normalized =
    tile_load / max_tile_load_in_current_frame
```

where

```text
max_tile_load_in_current_frame
```

is computed once per frame.

---

## Rendering Method

Preferred implementation:

1. Reuse existing tile metadata buffer.
2. Add a lightweight fullscreen debug pass.
3. One pixel reads its corresponding tile load.
4. Convert to heatmap color.

Avoid:

* Per-pixel scans
* CPU-side visualization
* Extra copies

The heatmap should be generated entirely on GPU.

---

## Runtime Toggle

Add a runtime flag:

```cpp
bool g_show_tile_heatmap;
```

or equivalent renderer setting.

Behavior:

```text
OFF:
    normal renderer output

ON:
    heatmap visualization
```

The default should be OFF.

---

# Feature 2: Gaussian Statistics

## Goal

Collect per-frame:

```text
Visible Gaussian Count
Visible Gaussian Instance Count
```

Output To CSV
---

## Definitions

### Visible Gaussian Count

Number of unique Gaussians that survive visibility/culling and participate in rendering.

Equivalent to:

```text
count(visible gaussian IDs)
```

after culling.

---

### Visible Gaussian Instance Count

Number of tile entries generated after tiling/binning.

Equivalent to:

```text
sum(tile_entry_count)
```

or

```text
total number of (Gaussian, Tile) pairs
```

Example:

```text
Gaussian A -> 3 tiles
Gaussian B -> 5 tiles

Visible Gaussian Count = 2
Visible Gaussian Instance Count = 8
```

---

# Performance Requirements

This is critical.

Statistics collection must have near-zero overhead.

Preferred methods:

## Instance Count

Reuse existing counters already produced during:

```text
tile binning
tile entry generation
prefix sum
```

Do NOT iterate over all tiles on CPU.

Do NOT scan buffers every frame.

Use existing totals if available.

---

## Visible Gaussian Count

Reuse existing visible-Gaussian list length.

If a visibility pass already generates:

```text
visibleGaussianCount
```

simply expose it.

Avoid:

```text
extra GPU passes
extra reductions
extra readbacks
```

---

# GPU -> CPU Readback

Allowed:

```text
one small statistics buffer
```

containing:

```cpp
struct FrameStats
{
    uint32_t visibleGaussianCount;
    uint32_t visibleGaussianInstanceCount;
    uint32_t maxTileLoad;
};
```

Requirements:

* Persistently mapped
* Ring-buffered
* No GPU stall
* No vkQueueWaitIdle
* No vkDeviceWaitIdle

Use asynchronous readback.

Reading N-1 frame statistics is acceptable.

---

# Console Output

Print once per frame:

```text
Frame 1234

Visible Gaussians:        XXXXX
Gaussian Instances:      XXXXX
Max Tile Load:           XXXXX
```

Provide a runtime option to disable printing.

Recommended:

```cpp
bool g_print_frame_stats;
```

Default:

```text
OFF
```

---

# Debug Overlay (Optional)

If an existing ImGui overlay exists, expose:

```text
Visible Gaussians
Gaussian Instances
Max Tile Load
Average Tile Load
```

This is preferred over console spam.

---

# Validation

Create a validation checklist:

## Statistics

Verify:

```text
instance_count >= gaussian_count
```

always holds.

---

## Heatmap

Verify:

```text
dense regions -> warmer colors
sparse regions -> cooler colors
```

and

```text
max tile appears red
```

after normalization.

---

# Constraints

Do NOT:

* Change rendering results
* Introduce GPU synchronization
* Add CPU-side tile scans
* Add per-frame allocations
* Add vkQueueWaitIdle
* Add vkDeviceWaitIdle

Prefer:

* Reusing existing buffers
* Reusing existing counters
* GPU-only heatmap generation
* Asynchronous statistics readback

---

# Deliverables

1. Tile heatmap rendering mode.
2. Runtime toggle for heatmap.
3. Per-frame visible Gaussian count.
4. Per-frame Gaussian instance count.
5. Max tile load statistic.
6. Asynchronous readback implementation.
7. Brief documentation describing:

   * where counters are produced
   * how heatmap is generated
   * measured overhead
