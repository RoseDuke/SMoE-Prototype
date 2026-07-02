#!/usr/bin/env python3
"""Generate SVG figures for SmartNIC optimization sweep results.

The script intentionally uses only the Python standard library so it can run on
cluster login nodes without matplotlib or other plotting dependencies.
"""

import argparse
import csv
import html
import math
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


CONFIGS: Sequence[Tuple[str, str]] = (
    ("synchronous_baseline", "Sync baseline"),
    ("async_only", "Async send"),
    ("async_aggregation", "Async + agg"),
    ("async_expert_counter", "Async + counter"),
    ("full_smartnic", "Full SmartNIC"),
)

COLORS = {
    "synchronous_baseline": "#5b6472",
    "async_only": "#2f7ebc",
    "async_aggregation": "#37a36b",
    "async_expert_counter": "#d28a1e",
    "full_smartnic": "#b84a62",
    "p50": "#4f7cac",
    "p95": "#53a548",
    "p99": "#c45b35",
    "axis": "#31363f",
    "grid": "#d8dee8",
    "text": "#1f252e",
    "muted": "#6f7a89",
    "bg": "#ffffff",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input-dir",
        default="results/optimization_sweep_run001",
        help="Directory containing *_summary.txt and per-token CSV files.",
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help="Figure output directory. Defaults to INPUT_DIR/figures.",
    )
    return parser.parse_args()


def parse_number(raw: str) -> float:
    raw = raw.strip()
    if "." in raw:
        return float(raw)
    return float(int(raw))


def read_summary(path: Path) -> Dict[str, object]:
    data: Dict[str, object] = {"destinations": {}}
    current_dst = None
    with path.open("r", encoding="utf-8") as fh:
        for raw_line in fh:
            line = raw_line.rstrip()
            if not line:
                continue
            if line.startswith("Destination "):
                current_dst = int(line.split()[1].rstrip(":"))
                data["destinations"][current_dst] = {}
                continue
            if ":" not in line:
                continue

            key, value = line.split(":", 1)
            normalized = key.strip().lower().replace(" ", "_").replace("/", "_")
            if current_dst is None:
                data[normalized] = parse_number(value)
            else:
                data["destinations"][current_dst][normalized] = parse_number(value)
    return data


def read_summaries(input_dir: Path) -> Dict[str, Dict[str, object]]:
    summaries = {}
    for key, _label in CONFIGS:
        path = input_dir / f"{key}_summary.txt"
        if not path.exists():
            raise FileNotFoundError(f"missing summary: {path}")
        summaries[key] = read_summary(path)
    return summaries


def read_csv_rows(path: Path) -> List[Dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as fh:
        return list(csv.DictReader(fh))


def fmt_int(value: float) -> str:
    return f"{int(round(value)):,}"


def fmt_pct(value: float) -> str:
    return f"{value * 100:.1f}%"


def pct_change(current: float, baseline: float) -> float:
    return (current - baseline) / baseline


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def svg_document(width: int, height: int, body: Iterable[str]) -> str:
    return "\n".join(
        [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
            "<style>",
            "text{font-family:Arial,Helvetica,sans-serif;fill:#1f252e}",
            ".title{font-size:22px;font-weight:700}",
            ".subtitle{font-size:13px;fill:#6f7a89}",
            ".axis{stroke:#31363f;stroke-width:1.2}",
            ".grid{stroke:#d8dee8;stroke-width:1}",
            ".tick{font-size:11px;fill:#6f7a89}",
            ".label{font-size:12px;fill:#1f252e}",
            ".small{font-size:11px;fill:#6f7a89}",
            ".value{font-size:12px;font-weight:700;fill:#1f252e}",
            "</style>",
            f'<rect x="0" y="0" width="{width}" height="{height}" fill="{COLORS["bg"]}"/>',
            *body,
            "</svg>",
        ]
    )


def text(
    x: float,
    y: float,
    value: object,
    klass: str = "label",
    anchor: str = "start",
    rotate: Optional[float] = None,
) -> str:
    transform = ""
    if rotate is not None:
        transform = f' transform="rotate({rotate:.2f} {x:.2f} {y:.2f})"'
    return (
        f'<text x="{x:.2f}" y="{y:.2f}" class="{klass}" '
        f'text-anchor="{anchor}"{transform}>{esc(value)}</text>'
    )


def line(x1: float, y1: float, x2: float, y2: float, klass: str = "axis") -> str:
    return f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" class="{klass}"/>'


def rect(
    x: float,
    y: float,
    width: float,
    height: float,
    fill: str,
    stroke: str = "none",
) -> str:
    return (
        f'<rect x="{x:.2f}" y="{y:.2f}" width="{width:.2f}" '
        f'height="{height:.2f}" fill="{fill}" stroke="{stroke}"/>'
    )


def polyline(points: Sequence[Tuple[float, float]], color: str, width: float = 2.2) -> str:
    serialized = " ".join(f"{x:.2f},{y:.2f}" for x, y in points)
    return (
        f'<polyline points="{serialized}" fill="none" stroke="{color}" '
        f'stroke-width="{width}" stroke-linejoin="round" stroke-linecap="round"/>'
    )


def draw_y_axis(
    body: List[str],
    left: float,
    top: float,
    bottom: float,
    right: float,
    max_value: float,
    ticks: int,
    formatter,
) -> None:
    body.append(line(left, top, left, bottom))
    body.append(line(left, bottom, right, bottom))
    for tick in range(ticks + 1):
        value = max_value * tick / ticks
        y = bottom - (bottom - top) * value / max_value if max_value else bottom
        body.append(line(left, y, right, y, "grid"))
        body.append(text(left - 8, y + 4, formatter(value), "tick", "end"))


def figure_avg_latency(summaries: Dict[str, Dict[str, object]], out: Path) -> None:
    width, height = 980, 560
    left, right, top, bottom = 92, 930, 92, 430
    baseline = float(summaries["synchronous_baseline"]["average_latency_cycles"])
    values = [
        (key, label, float(summaries[key]["average_latency_cycles"]) / baseline)
        for key, label in CONFIGS
    ]

    body: List[str] = [
        text(36, 42, "Average latency improvement", "title"),
        text(
            36,
            64,
            "Lower normalized latency means less time from token arrival to completion.",
            "subtitle",
        ),
    ]
    draw_y_axis(body, left, top, bottom, right, 1.10, 5, lambda v: f"{v:.1f}x")

    gap = 34
    bar_w = (right - left - gap * (len(values) + 1)) / len(values)
    for idx, (key, label, normalized) in enumerate(values):
        x = left + gap + idx * (bar_w + gap)
        bar_h = (bottom - top) * normalized / 1.10
        y = bottom - bar_h
        body.append(rect(x, y, bar_w, bar_h, COLORS[key]))
        raw = float(summaries[key]["average_latency_cycles"])
        body.append(text(x + bar_w / 2, y - 22, f"{normalized:.3f}x", "value", "middle"))
        if key == "synchronous_baseline":
            body.append(text(x + bar_w / 2, y - 6, "baseline", "small", "middle"))
        else:
            reduction = -pct_change(raw, baseline)
            body.append(text(x + bar_w / 2, y - 6, f"{fmt_pct(reduction)} lower", "small", "middle"))
        body.append(text(x + bar_w / 2, bottom + 24, label, "label", "middle"))
        body.append(text(x + bar_w / 2, bottom + 42, fmt_int(raw), "small", "middle"))

    body.append(text(left, height - 48, "Baseline average latency: " + fmt_int(baseline) + " cycles", "subtitle"))
    out.write_text(svg_document(width, height, body), encoding="utf-8")


def figure_tail_latency(summaries: Dict[str, Dict[str, object]], out: Path) -> None:
    width, height = 1050, 590
    left, right, top, bottom = 88, 990, 96, 440
    metrics = [
        ("p50_latency_cycles", "P50", "p50"),
        ("p95_latency_cycles", "P95", "p95"),
        ("p99_latency_cycles", "P99", "p99"),
    ]
    max_value = max(float(summaries[key][metric]) for key, _ in CONFIGS for metric, _, _ in metrics)
    max_value = math.ceil(max_value / 1_000_000) * 1_000_000

    body: List[str] = [
        text(36, 42, "Tail latency by optimization stage", "title"),
        text(36, 64, "P50 improves strongly; P95/P99 improve more modestly on this trace.", "subtitle"),
    ]
    draw_y_axis(body, left, top, bottom, right, max_value, 6, lambda v: f"{v / 1_000_000:.1f}M")

    group_gap = 25
    group_w = (right - left - group_gap * (len(CONFIGS) + 1)) / len(CONFIGS)
    bar_gap = 5
    bar_w = (group_w - bar_gap * (len(metrics) - 1)) / len(metrics)
    for i, (key, label) in enumerate(CONFIGS):
        gx = left + group_gap + i * (group_w + group_gap)
        for j, (metric, metric_label, color_key) in enumerate(metrics):
            value = float(summaries[key][metric])
            bar_h = (bottom - top) * value / max_value
            x = gx + j * (bar_w + bar_gap)
            y = bottom - bar_h
            body.append(rect(x, y, bar_w, bar_h, COLORS[color_key]))
        body.append(text(gx + group_w / 2, bottom + 24, label, "label", "middle"))

    legend_x = left
    for idx, (_metric, label, color_key) in enumerate(metrics):
        x = legend_x + idx * 92
        body.append(rect(x, height - 72, 15, 15, COLORS[color_key]))
        body.append(text(x + 22, height - 60, label, "label"))

    full = summaries["full_smartnic"]
    base = summaries["synchronous_baseline"]
    note = (
        "Full vs baseline: P50 "
        f"{fmt_pct(-pct_change(float(full['p50_latency_cycles']), float(base['p50_latency_cycles'])))} lower, "
        f"P95 {fmt_pct(-pct_change(float(full['p95_latency_cycles']), float(base['p95_latency_cycles'])))} lower, "
        f"P99 {fmt_pct(-pct_change(float(full['p99_latency_cycles']), float(base['p99_latency_cycles'])))} lower."
    )
    body.append(text(left + 310, height - 60, note, "subtitle"))
    out.write_text(svg_document(width, height, body), encoding="utf-8")


def figure_packets_and_cycles(summaries: Dict[str, Dict[str, object]], out: Path) -> None:
    width, height = 1080, 590
    panel_top, panel_bottom = 104, 430
    left1, right1 = 82, 510
    left2, right2 = 620, 1025

    body: List[str] = [
        text(36, 42, "Packet reduction and end-to-end makespan", "title"),
        text(36, 64, "Aggregation cuts packet count sharply; final cycle improves only slightly on this trace.", "subtitle"),
    ]

    packets_max = max(float(summaries[key]["total_packets"]) for key, _ in CONFIGS)
    cycle_baseline = float(summaries["synchronous_baseline"]["final_cycle"])
    cycle_max = 1.02

    body.append(text((left1 + right1) / 2, 88, "Total packets", "label", "middle"))
    draw_y_axis(body, left1, panel_top, panel_bottom, right1, packets_max, 4, lambda v: f"{v / 1000:.1f}k")
    body.append(text((left2 + right2) / 2, 88, "Final cycle normalized", "label", "middle"))
    draw_y_axis(body, left2, panel_top, panel_bottom, right2, cycle_max, 4, lambda v: f"{v:.2f}x")

    def draw_panel(left: float, right: float, max_value: float, metric: str, normalized: bool = False) -> None:
        gap = 16
        bar_w = (right - left - gap * (len(CONFIGS) + 1)) / len(CONFIGS)
        for idx, (key, label) in enumerate(CONFIGS):
            raw = float(summaries[key][metric])
            value = raw / cycle_baseline if normalized else raw
            bar_h = (panel_bottom - panel_top) * value / max_value
            x = left + gap + idx * (bar_w + gap)
            y = panel_bottom - bar_h
            body.append(rect(x, y, bar_w, bar_h, COLORS[key]))
            if metric == "total_packets":
                body.append(text(x + bar_w / 2, y - 8, fmt_int(raw), "small", "middle"))
            else:
                body.append(text(x + bar_w / 2, y - 8, f"{value:.3f}x", "small", "middle"))
            body.append(text(x + bar_w / 2, panel_bottom + 24, label, "small", "middle", rotate=-18))

    draw_panel(left1, right1, packets_max, "total_packets", normalized=False)
    draw_panel(left2, right2, cycle_max, "final_cycle", normalized=True)

    packet_reduction = -pct_change(
        float(summaries["full_smartnic"]["total_packets"]),
        float(summaries["synchronous_baseline"]["total_packets"]),
    )
    cycle_reduction = -pct_change(
        float(summaries["full_smartnic"]["final_cycle"]),
        float(summaries["synchronous_baseline"]["final_cycle"]),
    )
    body.append(text(82, height - 64, f"Full SmartNIC packet reduction: {fmt_pct(packet_reduction)}.", "subtitle"))
    body.append(text(620, height - 64, f"Full SmartNIC final-cycle reduction: {fmt_pct(cycle_reduction)}.", "subtitle"))
    out.write_text(svg_document(width, height, body), encoding="utf-8")


def cdf_points(values: Sequence[float], max_points: int = 250) -> List[Tuple[float, float]]:
    ordered = sorted(values)
    if not ordered:
        return []
    step = max(1, len(ordered) // max_points)
    points = [(ordered[idx], (idx + 1) / len(ordered)) for idx in range(0, len(ordered), step)]
    if points[-1][0] != ordered[-1]:
        points.append((ordered[-1], 1.0))
    return points


def figure_latency_cdf(input_dir: Path, out: Path) -> None:
    width, height = 1050, 620
    left, right, top, bottom = 92, 970, 94, 470
    per_config = {}
    for key, _label in CONFIGS:
        rows = read_csv_rows(input_dir / f"{key}.csv")
        per_config[key] = [float(row["total_latency"]) for row in rows]

    max_x = max(max(values) for values in per_config.values())
    max_x = math.ceil(max_x / 1_000_000) * 1_000_000

    body: List[str] = [
        text(36, 42, "Per-token latency CDF", "title"),
        text(36, 64, "Curves further left indicate more tokens completing earlier.", "subtitle"),
    ]
    body.append(line(left, top, left, bottom))
    body.append(line(left, bottom, right, bottom))
    for tick in range(6):
        x_value = max_x * tick / 5
        x = left + (right - left) * x_value / max_x
        body.append(line(x, top, x, bottom, "grid"))
        body.append(text(x, bottom + 22, f"{x_value / 1_000_000:.1f}M", "tick", "middle"))
    for tick in range(6):
        y_value = tick / 5
        y = bottom - (bottom - top) * y_value
        body.append(line(left, y, right, y, "grid"))
        body.append(text(left - 10, y + 4, f"{y_value:.1f}", "tick", "end"))

    for key, label in CONFIGS:
        points = []
        for latency, cdf in cdf_points(per_config[key]):
            x = left + (right - left) * latency / max_x
            y = bottom - (bottom - top) * cdf
            points.append((x, y))
        body.append(polyline(points, COLORS[key]))

    legend_y = height - 86
    legend_x = left
    for idx, (key, label) in enumerate(CONFIGS):
        x = legend_x + (idx % 3) * 245
        y = legend_y + (idx // 3) * 28
        body.append(rect(x, y - 13, 18, 4, COLORS[key]))
        body.append(text(x + 28, y - 7, label, "label"))
    body.append(text((left + right) / 2, bottom + 52, "Total latency cycles", "label", "middle"))
    body.append(text(34, (top + bottom) / 2, "CDF", "label", "middle", rotate=-90))
    out.write_text(svg_document(width, height, body), encoding="utf-8")


def figure_destination_skew(summaries: Dict[str, Dict[str, object]], out: Path) -> None:
    width, height = 980, 580
    left, right, top, bottom = 90, 920, 96, 430
    destinations = sorted(summaries["synchronous_baseline"]["destinations"].keys())
    token_values = [
        float(summaries["synchronous_baseline"]["destinations"][dst]["tokens_sent"])
        for dst in destinations
    ]
    packet_values = [
        float(summaries["full_smartnic"]["destinations"][dst]["packets_sent"])
        for dst in destinations
    ]
    max_value = max(token_values)

    body: List[str] = [
        text(36, 42, "Destination skew and aggregation effect", "title"),
        text(
            36,
            64,
            "Token load remains skewed; aggregation reduces packets but does not rebalance expert demand.",
            "subtitle",
        ),
    ]
    draw_y_axis(body, left, top, bottom, right, max_value, 5, lambda v: f"{v / 1000:.1f}k")

    group_gap = 58
    group_w = (right - left - group_gap * (len(destinations) + 1)) / len(destinations)
    bar_gap = 10
    bar_w = (group_w - bar_gap) / 2
    for idx, dst in enumerate(destinations):
        gx = left + group_gap + idx * (group_w + group_gap)
        for j, (value, color, label) in enumerate(
            (
                (token_values[idx], "#6d7480", "tokens"),
                (packet_values[idx], COLORS["full_smartnic"], "full packets"),
            )
        ):
            x = gx + j * (bar_w + bar_gap)
            bar_h = (bottom - top) * value / max_value
            y = bottom - bar_h
            body.append(rect(x, y, bar_w, bar_h, color))
            body.append(text(x + bar_w / 2, y - 8, fmt_int(value), "small", "middle"))
        body.append(text(gx + group_w / 2, bottom + 28, f"dst {dst}", "label", "middle"))

    body.append(rect(left, height - 72, 16, 16, "#6d7480"))
    body.append(text(left + 24, height - 59, "Tokens sent", "label"))
    body.append(rect(left + 148, height - 72, 16, 16, COLORS["full_smartnic"]))
    body.append(text(left + 172, height - 59, "Full SmartNIC packets", "label"))
    out.write_text(svg_document(width, height, body), encoding="utf-8")


def figure_counter_stall(summaries: Dict[str, Dict[str, object]], out: Path) -> None:
    width, height = 860, 520
    left, right, top, bottom = 90, 800, 94, 382
    destinations = sorted(summaries["full_smartnic"]["destinations"].keys())
    values = [
        float(summaries["full_smartnic"]["destinations"][dst]["counter_stall_cycles"])
        for dst in destinations
    ]
    max_value = max(values) if values else 1.0

    body: List[str] = [
        text(36, 42, "Counter stall side effect", "title"),
        text(36, 64, "Expert counters can intentionally defer sends; this is the modeled control cost.", "subtitle"),
    ]
    draw_y_axis(body, left, top, bottom, right, max_value, 4, lambda v: f"{v / 1000:.1f}k")

    gap = 48
    bar_w = (right - left - gap * (len(destinations) + 1)) / len(destinations)
    for idx, dst in enumerate(destinations):
        value = values[idx]
        x = left + gap + idx * (bar_w + gap)
        bar_h = (bottom - top) * value / max_value if max_value else 0
        y = bottom - bar_h
        body.append(rect(x, y, bar_w, bar_h, COLORS["full_smartnic"]))
        body.append(text(x + bar_w / 2, y - 10, fmt_int(value), "value", "middle"))
        body.append(text(x + bar_w / 2, bottom + 28, f"dst {dst}", "label", "middle"))

    total = float(summaries["full_smartnic"]["total_counter_stall_cycles"])
    body.append(text(left, height - 62, f"Total counter stall cycles: {fmt_int(total)}.", "subtitle"))
    out.write_text(svg_document(width, height, body), encoding="utf-8")


def figure_basic_moe_comparison(summaries: Dict[str, Dict[str, object]], out: Path) -> None:
    width, height = 1180, 680
    left, right, top, bottom = 88, 705, 112, 470
    staged = [
        ("synchronous_baseline", "Basic MoE\nsynchronous"),
        ("async_aggregation", "SmartNIC-side\nbatching"),
        ("full_smartnic", "Full\nSmartNIC"),
    ]
    baseline = summaries["synchronous_baseline"]
    avg_baseline = float(baseline["average_latency_cycles"])
    max_value = 1.05

    body: List[str] = [
        text(36, 44, "Basic MoE baseline vs SmartNIC optimizations", "title"),
        text(
            36,
            68,
            "Comparison against the original synchronous MoE execution model.",
            "subtitle",
        ),
        text(88, 96, "Average latency normalized to basic MoE baseline", "label"),
    ]
    draw_y_axis(body, left, top, bottom, right, max_value, 5, lambda v: f"{v:.1f}x")

    gap = 34
    bar_w = (right - left - gap * (len(staged) + 1)) / len(staged)
    for idx, (key, label) in enumerate(staged):
        raw = float(summaries[key]["average_latency_cycles"])
        normalized = raw / avg_baseline
        reduction = -pct_change(raw, avg_baseline)
        x = left + gap + idx * (bar_w + gap)
        bar_h = (bottom - top) * normalized / max_value
        y = bottom - bar_h
        body.append(rect(x, y, bar_w, bar_h, COLORS[key]))
        body.append(text(x + bar_w / 2, y - 24, f"{normalized:.3f}x", "value", "middle"))
        if key == "synchronous_baseline":
            body.append(text(x + bar_w / 2, y - 8, "baseline", "small", "middle"))
        else:
            body.append(text(x + bar_w / 2, y - 8, f"{fmt_pct(reduction)} lower", "small", "middle"))
        first, second = label.split("\n")
        body.append(text(x + bar_w / 2, bottom + 26, first, "label", "middle"))
        body.append(text(x + bar_w / 2, bottom + 43, second, "label", "middle"))
        body.append(text(x + bar_w / 2, bottom + 62, fmt_int(raw) + " cycles", "small", "middle"))

    card_x, card_y = 760, 112
    card_w, card_h = 365, 358
    body.append(rect(card_x, card_y, card_w, card_h, "#f7f9fc", "#d8dee8"))
    body.append(text(card_x + 22, card_y + 34, "Improvement vs basic MoE baseline", "label"))

    batched = summaries["async_aggregation"]
    full = summaries["full_smartnic"]
    rows = [
        ("Batched avg latency", batched, "average_latency_cycles"),
        ("Full avg latency", full, "average_latency_cycles"),
        ("Full P50 latency", full, "p50_latency_cycles"),
        ("Full P95 latency", full, "p95_latency_cycles"),
        ("Full packet count", full, "total_packets"),
        ("Full final cycle", full, "final_cycle"),
    ]
    y = card_y + 72
    for label, optimized, metric in rows:
        base_value = float(baseline[metric])
        optimized_value = float(optimized[metric])
        reduction = -pct_change(optimized_value, base_value)
        body.append(text(card_x + 22, y, label, "small"))
        body.append(text(card_x + 190, y, fmt_int(base_value) + " -> " + fmt_int(optimized_value), "small"))
        body.append(text(card_x + card_w - 22, y, fmt_pct(reduction), "value", "end"))
        y += 40

    note_y = 540
    body.append(text(88, note_y, "Interpretation:", "label"))
    body.append(text(88, note_y + 23, "SmartNIC-side batching reduces packet overhead while preserving batched GPU execution.", "subtitle"))
    body.append(text(88, note_y + 45, "Full SmartNIC adds admission control and reorder logic on top of batching.", "subtitle"))
    body.append(text(88, note_y + 67, "The main measured gain is average/P50 latency; final-cycle improvement remains modest on this trace.", "subtitle"))
    out.write_text(svg_document(width, height, body), encoding="utf-8")


def write_index(summaries: Dict[str, Dict[str, object]], output_dir: Path) -> None:
    baseline = summaries["synchronous_baseline"]
    batched = summaries["async_aggregation"]
    full = summaries["full_smartnic"]
    batched_avg_reduction = -pct_change(
        float(batched["average_latency_cycles"]),
        float(baseline["average_latency_cycles"]),
    )
    avg_reduction = -pct_change(
        float(full["average_latency_cycles"]),
        float(baseline["average_latency_cycles"]),
    )
    packet_reduction = -pct_change(float(full["total_packets"]), float(baseline["total_packets"]))
    final_reduction = -pct_change(float(full["final_cycle"]), float(baseline["final_cycle"]))
    counter_stall = float(full["total_counter_stall_cycles"])

    lines = [
        "# SmartNIC Optimization Figures",
        "",
        "Generated from the optimization sweep summaries and per-token CSV files.",
        "",
        "## Main takeaways",
        "",
        f"- SmartNIC-side batching average latency reduction vs basic MoE baseline: {fmt_pct(batched_avg_reduction)}.",
        f"- Full SmartNIC average latency reduction vs basic MoE baseline: {fmt_pct(avg_reduction)}.",
        f"- Full SmartNIC packet reduction vs basic MoE baseline: {fmt_pct(packet_reduction)}.",
        f"- Full SmartNIC final-cycle reduction vs basic MoE baseline: {fmt_pct(final_reduction)}.",
        f"- Modeled counter-stall side effect in full SmartNIC: {fmt_int(counter_stall)} cycles.",
        "",
        "## Figures",
        "",
        "- `01_average_latency.svg`: staged average-latency improvement.",
        "- `02_tail_latency.svg`: P50/P95/P99 latency comparison.",
        "- `03_packets_and_cycles.svg`: packet count and makespan comparison.",
        "- `04_latency_cdf.svg`: per-token latency CDF.",
        "- `05_destination_skew.svg`: destination skew and packet coalescing.",
        "- `06_counter_stall.svg`: counter-stall side effect by destination.",
        "- `07_basic_moe_comparison.svg`: basic synchronous MoE baseline vs SmartNIC optimizations.",
        "",
    ]
    (output_dir / "README.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    args = parse_args()
    input_dir = Path(args.input_dir)
    output_dir = Path(args.output_dir) if args.output_dir else input_dir / "figures"
    output_dir.mkdir(parents=True, exist_ok=True)

    summaries = read_summaries(input_dir)
    figure_avg_latency(summaries, output_dir / "01_average_latency.svg")
    figure_tail_latency(summaries, output_dir / "02_tail_latency.svg")
    figure_packets_and_cycles(summaries, output_dir / "03_packets_and_cycles.svg")
    figure_latency_cdf(input_dir, output_dir / "04_latency_cdf.svg")
    figure_destination_skew(summaries, output_dir / "05_destination_skew.svg")
    figure_counter_stall(summaries, output_dir / "06_counter_stall.svg")
    figure_basic_moe_comparison(summaries, output_dir / "07_basic_moe_comparison.svg")
    write_index(summaries, output_dir)

    print(f"Wrote figures to {output_dir}")


if __name__ == "__main__":
    main()
