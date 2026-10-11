"""Writes a printable SVG template for the dome magnet ring.

Usage: python3 tools/magnet_template.py DIAMETER_MM [OUTPUT.svg]

DIAMETER_MM is the diameter of the circle the magnet centres sit on, on the
body's top surface. Print at 100% scale and check the 100 mm bar with a ruler.
For circles larger than the paper, use the print dialog's poster or tile option.
"""

import math
import sys
from pathlib import Path

# Degrees clockwise from front, seen from above. Must match kMagnetDeg in
# AstroPixelsPlus/R2DomeTracker.h.
MAGNETS = [0, 5, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330]
MARGIN_MM = 25
MAGNET_MARK_MM = 10


def magnet_positions(diameter_mm):
    """(degrees, x, y) for each magnet, relative to the centre, front up the page (-y)."""
    r = diameter_mm / 2
    return [(deg, r * math.sin(math.radians(deg)), -r * math.cos(math.radians(deg))) for deg in MAGNETS]


def template_svg(diameter_mm):
    size = diameter_mm + 2 * MARGIN_MM
    height = size + 30
    c = size / 2
    r = diameter_mm / 2
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{size:g}mm" height="{height:g}mm" '
        f'viewBox="0 0 {size:g} {height:g}" font-family="sans-serif">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<circle cx="{c:.2f}" cy="{c:.2f}" r="{r:.2f}" fill="none" stroke="#999" stroke-width="0.3"/>',
        f'<line x1="{c - 5:.2f}" y1="{c:.2f}" x2="{c + 5:.2f}" y2="{c:.2f}" stroke="black" stroke-width="0.3"/>',
        f'<line x1="{c:.2f}" y1="{c - 5:.2f}" x2="{c:.2f}" y2="{c + 5:.2f}" stroke="black" stroke-width="0.3"/>',
        f'<line x1="{c:.2f}" y1="{c - 10:.2f}" x2="{c:.2f}" y2="{c - r + 12:.2f}" stroke="black" stroke-width="0.5"/>',
        f'<text x="{c:.2f}" y="{c - r - 9:.2f}" font-size="6" text-anchor="middle">FRONT</text>',
    ]
    for deg, x, y in magnet_positions(diameter_mm):
        px, py = c + x, c + y
        out.append(f'<circle class="magnet" cx="{px:.2f}" cy="{py:.2f}" r="{MAGNET_MARK_MM / 2:g}" '
                   'fill="none" stroke="black" stroke-width="0.4"/>')
        out.append(f'<line x1="{px - 2:.2f}" y1="{py:.2f}" x2="{px + 2:.2f}" y2="{py:.2f}" stroke="black" stroke-width="0.2"/>')
        out.append(f'<line x1="{px:.2f}" y1="{py - 2:.2f}" x2="{px:.2f}" y2="{py + 2:.2f}" stroke="black" stroke-width="0.2"/>')
        lx = c + x * (r - 12) / r
        ly = c + y * (r - 12) / r
        out.append(f'<text x="{lx:.2f}" y="{ly + 1.5:.2f}" font-size="3.5" text-anchor="middle">{deg}°</text>')
    out += [
        f'<text x="{MARGIN_MM}" y="{size + 6:.2f}" font-size="3.5">Seen from above. One magnet on each mark, all the same face up. '
        f'Circle {diameter_mm:g} mm. The double mark at FRONT is 0° and 5° clockwise.</text>',
        f'<line class="scale" x1="{MARGIN_MM:.2f}" y1="{size + 15:.2f}" x2="{MARGIN_MM + 100:.2f}" y2="{size + 15:.2f}" '
        'stroke="black" stroke-width="0.6"/>',
        f'<text x="{MARGIN_MM}" y="{size + 22:.2f}" font-size="3.5">100 mm: check with a ruler before use</text>',
        "</svg>",
    ]
    return "\n".join(out) + "\n"


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    diameter = float(argv[1])
    output = Path(argv[2]) if len(argv) > 2 else Path(f"magnet_ring_{diameter:g}mm.svg")
    output.write_text(template_svg(diameter))
    print(f"Wrote {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
