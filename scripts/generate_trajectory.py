import json
import numpy as np
import sys
import argparse
# 示例：python generate_trajectory.py --axis z --up_angle 20 --radius 3.0 --height 2.0 --fov_deg 60 --num_frames 360 --output transforms_topdown.json
def generate_circular_trajectory(
    radius=5.0,
    height=0.0,
    num_frames=60,
    up_angle_deg=0.0,
    camera_angle_x=0.857,  # radians
    output_path="transforms_render.json",
    axis='y'  # 'x', 'y', or 'z'
):
    """
    Generate circular camera trajectory around origin.
    Axis specifies the rotation axis: 
        'y': classic NeRF (Y up, rotate in XZ plane)
        'z': Z up (rotate in XY plane) — useful for aerial/top-down views
        'x': X up (rotate in YZ plane)
    """
    frames = []
    world_up = np.array([0.0, 1.0, 0.0])  # 永远Y-up
    # Define world up vector based on rotation axis
    if axis == 'y':
        def pos_func(theta):
            return np.array([radius * np.cos(theta), height, radius * np.sin(theta)])
    elif axis == 'z':
        def pos_func(theta):
            return np.array([radius * np.cos(theta), radius * np.sin(theta), height])
    elif axis == 'x':
        def pos_func(theta):
            return np.array([height, radius * np.cos(theta), radius * np.sin(theta)])
    else:
        raise ValueError("axis must be 'x', 'y', or 'z'")
    
    for i in range(num_frames):
        theta = 2.0 * np.pi * i / num_frames
        camera_pos = pos_func(theta)
        look_at = np.array([0.0, 0.0, 0.0])

        # Forward: from camera to look-at (in OpenGL/Blender: -Z is forward)
        forward = look_at - camera_pos
        forward = forward / np.linalg.norm(forward)

        # Right = normalize(cross(forward, world_up))
        right = np.cross(forward, world_up)
        if np.linalg.norm(right) < 1e-6:
            # Camera is aligned with world_up (e.g., top-down view); use alternative up
            alt_up = np.array([1.0, 0.0, 0.0]) if axis != 'x' else np.array([0.0, 1.0, 0.0])
            right = np.cross(forward, alt_up)
        right = right / np.linalg.norm(right)

        # Recompute up to ensure orthogonality
        up = np.cross(right, forward)
        up = up / np.linalg.norm(up)

        # Optional tilt (around right axis)
        if up_angle_deg != 0:
            angle_rad = np.radians(up_angle_deg)
            cos_a, sin_a = np.cos(angle_rad), np.sin(angle_rad)
            forward_new = cos_a * forward + sin_a * up
            up_new = -sin_a * forward + cos_a * up
            forward, up = forward_new, up_new

        # Build C2W matrix (OpenGL/Blender convention)
        c2w = np.eye(4)
        c2w[:3, 0] = right      # X axis (right)
        c2w[:3, 1] = up
        c2w[:3, 2] = -forward
        c2w[:3, 3] = camera_pos

        frames.append({
            "file_path": f"./render/{i:04d}",
            "transform_matrix": c2w.tolist()
        })

    out = {
        "camera_angle_x": float(camera_angle_x),
        "frames": frames,
        "meta": {
            "cmd": " ".join(sys.argv),
            "radius": radius,
            "height": height,
            "num_frames": num_frames,
            "up_angle_deg": up_angle_deg,
            "fov_deg": float(np.degrees(camera_angle_x)),
            "axis": axis
        }
    }
    with open(output_path, "w") as f:
        json.dump(out, f, indent=2)

    print(f"✅ Saved {axis}-axis trajectory to {output_path}")
    print(f"   FOVx = {np.degrees(camera_angle_x):.2f} deg")
    print(f"   Frames: {num_frames}, Radius: {radius}, Height (along {axis}): {height}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate circular camera trajectory (NeRF/3DGS format).")
    parser.add_argument("--radius", type=float, default=5.0, help="Orbit radius")
    parser.add_argument("--height", type=float, default=0.0, help="Camera coordinate along rotation axis")
    parser.add_argument("--num_frames", type=int, default=60)
    parser.add_argument("--up_angle", type=float, default=0.0, help="Tilt angle in degrees (pitch)")
    parser.add_argument("--fov_deg", type=float, default=49.0, help="Horizontal FOV in degrees")
    parser.add_argument("--output", type=str, default="transforms_render.json")
    parser.add_argument("--axis", choices=['x', 'y', 'z'], default='y',
                        help="Rotation axis: 'y' (default, ground orbit), 'z' (top-down orbit), 'x' (side orbit)")

    args = parser.parse_args()

    generate_circular_trajectory(
        radius=args.radius,
        height=args.height,
        num_frames=args.num_frames,
        up_angle_deg=args.up_angle,
        camera_angle_x=np.radians(args.fov_deg),
        output_path=args.output,
        axis=args.axis
    )