import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

# ================= 配置区 =================
CSV_PATH = "profile.csv"       # CSV 文件路径
OUTPUT_PATH = "latency_scatter.png" # 输出图片路径
MAX_POINTS = 5000                 # 最大绘制点数(防止过密), 设为 None 则不采样
DPI = 300                         # 输出分辨率
# ==========================================


def load_and_sample(csv_path: str, max_points: int | None) -> pd.DataFrame:
    """读取CSV并按需下采样"""
    df = pd.read_csv(csv_path)
    required = {"frame_latency_ms", "visible_gaussians", "gaussian_instances"}
    missing = required - set(df.columns)
    if missing:
        raise ValueError(f"CSV 缺少必要列: {missing}")

    if max_points and len(df) > max_points:
        print(f"⚠️ 数据量({len(df)})超过阈值, 随机采样 {max_points} 个点用于绘图")
        df = df.sample(n=max_points, random_state=42).reset_index(drop=True)
    return df


def add_trendline(ax, x, y, color, label_suffix=""):
    """添加多项式趋势线以展示整体相关性"""
    mask = np.isfinite(x) & np.isfinite(y)
    if mask.sum() < 10:
        return
    coeffs = np.polyfit(x[mask], y[mask], deg=2)
    poly = np.poly1d(coeffs)
    x_sorted = np.sort(x[mask])
    ax.plot(x_sorted, poly(x_sorted), "--", color=color, linewidth=1.5,
            alpha=0.9, label=f"Trend{label_suffix}")


def plot_latency(df: pd.DataFrame, output_path: str, dpi: int) -> None:
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

    scatter_kwargs = dict(s=8, alpha=0.35, edgecolors="none", rasterized=True)

    # --- 左图: visible_gaussians vs latency ---
    sc1 = ax1.scatter(
        df["visible_gaussians"], df["frame_latency_ms"],
        c="#2171B5", **scatter_kwargs
    )
    add_trendline(ax1, df["visible_gaussians"].values,
                  df["frame_latency_ms"].values, "#08306B")
    ax1.set_xlabel("Visible Gaussians", fontsize=13)
    ax1.set_ylabel("Frame Latency (ms)", fontsize=13)
    ax1.set_title("Latency vs Visible Gaussians", fontsize=14)
    ax1.legend(markerscale=3, fontsize=10)
    ax1.grid(True, linestyle="--", alpha=0.4)

    # --- 右图: gaussian_instances vs latency ---
    sc2 = ax2.scatter(
        df["gaussian_instances"], df["frame_latency_ms"],
        c="#CB181D", **scatter_kwargs
    )
    add_trendline(ax2, df["gaussian_instances"].values,
                  df["frame_latency_ms"].values, "#67000D")
    ax2.set_xlabel("Gaussian Instances", fontsize=13)
    ax2.set_ylabel("Frame Latency (ms)", fontsize=13)
    ax2.set_title("Latency vs Gaussian Instances", fontsize=14)
    ax2.legend(markerscale=3, fontsize=10)
    ax2.grid(True, linestyle="--", alpha=0.4)

    plt.tight_layout()
    plt.savefig(output_path, dpi=dpi, bbox_inches="tight")
    print(f"✅ 图表已保存至: {output_path}")
    plt.show()


def main():
    csv_path = Path(CSV_PATH)
    if not csv_path.exists():
        raise FileNotFoundError(f"未找到文件: {csv_path}")

    df = load_and_sample(str(csv_path), MAX_POINTS)
    print(f"📊 数据概览:\n{df[['visible_gaussians','gaussian_instances','frame_latency_ms']].describe()}\n")
    plot_latency(df, OUTPUT_PATH, DPI)


if __name__ == "__main__":
    main()