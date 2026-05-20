# 3DGS Debugging Instructions

## Goal
Improve rendering quality to match Example/Playroom reference.

## Key Areas
- shaders in src/shaders
- Camera in src/Renderer.h
- offscreen outputs in ./tmp

## Workflow
1. Analyze shaders
2. Analyze camera projection
3. Compare tmp outputs with reference images
4. Modify minimal code changes
5. Validate via rerender

## Constraints
- No large refactors
- Minimal diff changes
- Always validate visually via tmp outputs

## Execution and Comparison

To Execute the renderer , run `run.sh`.
To see the effect , refer to example/image.png
render result will be dumped in tmp . 

Auto-run read-only operations (ls, cat, grep).
Require confirmation for any write or build operations.