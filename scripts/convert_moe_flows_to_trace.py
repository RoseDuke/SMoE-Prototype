#!/usr/bin/env python3
"""Convert MoE data-graph expert flows into the SmartNIC prototype trace CSV.

The input is the `data_graph/expert_flows.csv` produced by
`smartnic_input_constructor`.  The output is the C++ reference model trace:

arrival_cycle,token_id,batch_id,layer_id,src_rank,dst_rank,expert_id,payload_bytes

By default each expert-flow row becomes one dispatchable chunk.  This matches
the current run001 reference trace and avoids inventing per-token timing that
the extracted data does not contain.
"""

import argparse
import csv
from collections import namedtuple
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
from typing import Dict, Iterable, List


EXPECTED_FIELDS = {
    "flow_id",
    "decode_call",
    "layer_id",
    "microbatch_id",
    "src_rank",
    "dst_rank",
    "expert_id",
    "token_count",
    "payload_bytes",
    "ready_time_us",
}

TRACE_HEADER = [
    "arrival_cycle",
    "token_id",
    "batch_id",
    "layer_id",
    "src_rank",
    "dst_rank",
    "expert_id",
    "payload_bytes",
]

FlowRow = namedtuple(
    "FlowRow",
    [
        "sequence",
        "decode_call",
        "layer_id",
        "microbatch_id",
        "src_rank",
        "dst_rank",
        "expert_id",
        "token_count",
        "payload_bytes",
        "ready_time_us",
    ],
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert MoE expert flows into SmartNIC prototype trace CSV."
    )
    parser.add_argument(
        "--graph",
        type=Path,
        required=True,
        help="Path to a data_graph directory containing expert_flows.csv.",
    )
    parser.add_argument(
        "--out",
        type=Path,
        required=True,
        help="Output trace CSV path.",
    )
    parser.add_argument(
        "--cycles-per-us",
        type=Decimal,
        default=Decimal("1000"),
        help="Cycle conversion factor for ready_time_us. Default: 1000.",
    )
    parser.add_argument(
        "--preserve-decode-call-batches",
        action="store_true",
        help=(
            "Map decode_call to dense batch IDs. Default collapses all decode "
            "calls into batch_id=0 because the extracted ready times are "
            "phase-relative, not a global decode-call timeline."
        ),
    )
    parser.add_argument(
        "--expand-token-count",
        action="store_true",
        help=(
            "Expand each flow into token_count descriptors. Default keeps one "
            "descriptor per flow/chunk."
        ),
    )
    return parser.parse_args()


def parse_int(row: Dict[str, str], field: str, line_number: int) -> int:
    try:
        value = int(row[field])
    except (KeyError, ValueError) as exc:
        raise ValueError(f"invalid integer field {field!r} on line {line_number}") from exc
    return value


def read_flows(path: Path) -> List[FlowRow]:
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            raise ValueError(f"{path} is empty")
        missing = EXPECTED_FIELDS.difference(reader.fieldnames)
        if missing:
            missing_list = ", ".join(sorted(missing))
            raise ValueError(f"{path} is missing fields: {missing_list}")

        flows = []
        for sequence, row in enumerate(reader):
            line_number = sequence + 2
            try:
                ready_time_us = Decimal(row["ready_time_us"])
            except Exception as exc:  # Decimal raises several conversion errors.
                raise ValueError(
                    f"invalid ready_time_us on line {line_number}: {row.get('ready_time_us')}"
                ) from exc

            flow = FlowRow(
                sequence=sequence,
                decode_call=parse_int(row, "decode_call", line_number),
                layer_id=parse_int(row, "layer_id", line_number),
                microbatch_id=parse_int(row, "microbatch_id", line_number),
                src_rank=parse_int(row, "src_rank", line_number),
                dst_rank=parse_int(row, "dst_rank", line_number),
                expert_id=parse_int(row, "expert_id", line_number),
                token_count=parse_int(row, "token_count", line_number),
                payload_bytes=parse_int(row, "payload_bytes", line_number),
                ready_time_us=ready_time_us,
            )
            if flow.token_count <= 0:
                raise ValueError(f"token_count must be positive on line {line_number}")
            if flow.payload_bytes <= 0:
                raise ValueError(f"payload_bytes must be positive on line {line_number}")
            if flow.ready_time_us < 0:
                raise ValueError(f"ready_time_us must be non-negative on line {line_number}")
            flows.append(flow)

    if not flows:
        raise ValueError(f"{path} contains no flow rows")
    return flows


def to_cycle(value_us: Decimal, cycles_per_us: Decimal) -> int:
    cycles = (value_us * cycles_per_us).quantize(Decimal("1"), rounding=ROUND_HALF_UP)
    return int(cycles)


def split_payload(payload_bytes: int, token_count: int) -> Iterable[int]:
    base = payload_bytes // token_count
    remainder = payload_bytes % token_count
    for index in range(token_count):
        yield base + (1 if index < remainder else 0)


def build_trace_rows(
    flows: List[FlowRow],
    cycles_per_us: Decimal,
    preserve_decode_call_batches: bool,
    expand_token_count: bool,
) -> List[List[int]]:
    decode_to_batch = {
        decode_call: batch_id
        for batch_id, decode_call in enumerate(sorted({flow.decode_call for flow in flows}))
    }

    rows = []
    token_id = 0
    sorted_flows = sorted(
        flows,
        key=lambda flow: (
            to_cycle(flow.ready_time_us, cycles_per_us),
            flow.sequence,
        ),
    )

    for flow in sorted_flows:
        arrival_cycle = to_cycle(flow.ready_time_us, cycles_per_us)
        batch_id = decode_to_batch[flow.decode_call] if preserve_decode_call_batches else 0
        payloads = (
            split_payload(flow.payload_bytes, flow.token_count)
            if expand_token_count
            else (flow.payload_bytes,)
        )
        for payload_bytes in payloads:
            rows.append(
                [
                    arrival_cycle,
                    token_id,
                    batch_id,
                    flow.layer_id,
                    flow.src_rank,
                    flow.dst_rank,
                    flow.expert_id,
                    payload_bytes,
                ]
            )
            token_id += 1

    return rows


def write_trace(path: Path, rows: List[List[int]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(TRACE_HEADER)
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    graph_dir = args.graph
    flows_path = graph_dir / "expert_flows.csv"
    flows = read_flows(flows_path)
    rows = build_trace_rows(
        flows=flows,
        cycles_per_us=args.cycles_per_us,
        preserve_decode_call_batches=args.preserve_decode_call_batches,
        expand_token_count=args.expand_token_count,
    )
    write_trace(args.out, rows)
    print(f"Wrote SmartNIC prototype trace: {args.out}")
    print(f"Input flows: {len(flows)}")
    print(f"Trace descriptors: {len(rows)}")
    print(f"Cycles per us: {args.cycles_per_us}")
    print(
        "Batch mapping: "
        + ("preserve decode_call as dense batch IDs" if args.preserve_decode_call_batches else "collapsed to batch_id=0")
    )
    print("Granularity: " + ("expanded per routed token" if args.expand_token_count else "one descriptor per expert flow"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
