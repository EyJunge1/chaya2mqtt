#!/usr/bin/env python3
"""Apply a KiCad XML netlist to the matching PCB footprints.

Run this script with KiCad's bundled Python so the ``pcbnew`` module is
available.  It is intentionally limited to net assignments; placement,
footprint geometry, and routing remain unchanged.
"""

from __future__ import annotations

import argparse
import xml.etree.ElementTree as ET
from pathlib import Path

import pcbnew


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("netlist", type=Path)
    parser.add_argument("board", type=Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    board = pcbnew.LoadBoard(str(args.board))
    assignments: dict[tuple[str, str], str] = {}

    for net in ET.parse(args.netlist).getroot().findall("./nets/net"):
        name = net.get("name")
        if not name:
            continue
        for node in net.findall("node"):
            reference = node.get("ref")
            pin = node.get("pin")
            if reference and pin:
                assignments[(reference, pin)] = name

    net_by_name = {
        net.GetNetname(): net
        for net in board.GetNetInfo().NetsByName().values()
    }
    missing_footprints: set[str] = set()
    missing_pads: list[str] = []
    applied = 0

    for (reference, pin), net_name in assignments.items():
        footprint = board.FindFootprintByReference(reference)
        if footprint is None:
            missing_footprints.add(reference)
            continue
        pads = [pad for pad in footprint.Pads() if pad.GetNumber() == pin]
        if not pads:
            missing_pads.append(f"{reference}.{pin}")
            continue
        net = net_by_name.get(net_name)
        if net is None:
            net = pcbnew.NETINFO_ITEM(board, net_name)
            board.Add(net)
            net_by_name[net_name] = net
        for pad in pads:
            pad.SetNet(net)
            applied += 1

    if missing_footprints or missing_pads:
        details = []
        if missing_footprints:
            details.append(
                "missing footprints: " + ", ".join(sorted(missing_footprints))
            )
        if missing_pads:
            details.append("missing pads: " + ", ".join(sorted(missing_pads)))
        raise RuntimeError("; ".join(details))

    if not pcbnew.SaveBoard(str(args.board), board):
        raise RuntimeError(f"Failed to save {args.board}")
    print(f"Applied {applied} pad net assignments to {args.board}")


if __name__ == "__main__":
    main()
