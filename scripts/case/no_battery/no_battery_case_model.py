"""USB-powered case without battery for Waveshare ESP32-S3-ePaper-1.54(G).

Variant: scripts/case/no_battery/
Sibling: scripts/case/battery/ (602530 battery + dock)

CadQuery 2.x - `python3 scripts/case/no_battery/no_battery_case_model.py`
  writes print-ready STLs to ./out (lid outer face on the bed).

TODO: port geometry from the battery case with a thinner back (no cell pocket).
"""

from __future__ import annotations


def main() -> int:
    """Export print-ready STLs once the model exists."""
    raise NotImplementedError("no-battery case geometry is not modeled yet")


if __name__ == "__main__":
    raise SystemExit(main())
