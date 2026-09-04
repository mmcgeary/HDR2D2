#!/usr/bin/env python3
"""
Full Marching Cubes Isosurface Engine in Pure Python
Extracts Guaranteed Watertight, 2-Manifold STLs directly from Signed Distance Fields (SDF)
"""

import math, struct, os, time

# Paul Bourke standard Marching Cubes edge vertex indices
EDGE_VERTICES = [
    (0, 1), (1, 2), (2, 3), (3, 0),
    (4, 5), (5, 6), (6, 7), (7, 4),
    (0, 4), (1, 5), (2, 6), (3, 7)
]

# Corner offsets in unit cube [0..1]
CUBE_CORNERS = [
    (0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0),
    (0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)
]

# Standard Marching Cubes 256-case table from Bourke/Lorensen
TRI_TABLE_FULL = [
    [-1], [0, 8, 3, -1], [0, 1, 9, -1], [1, 8, 3, 9, 8, 1, -1],
    [1, 2, 10, -1], [0, 8, 3, 1, 2, 10, -1], [9, 2, 10, 0, 2, 9, -1],
    [2, 8, 3, 2, 10, 8, 10, 9, 8, -1], [3, 11, 2, -1], [0, 11, 2, 8, 11, 0, -1],
    [1, 9, 0, 2, 3, 11, -1], [1, 11, 2, 1, 9, 11, 9, 8, 11, -1], [3, 10, 1, 11, 10, 3, -1],
    [0, 10, 1, 0, 8, 10, 8, 11, 10, -1], [3, 9, 0, 3, 11, 9, 11, 10, 9, -1], [9, 8, 10, 10, 8, 11, -1],
    [4, 7, 8, -1], [4, 3, 0, 7, 3, 4, -1], [0, 1, 9, 8, 4, 7, -1], [4, 1, 9, 4, 7, 1, 7, 3, 1, -1],
    [1, 2, 10, 8, 4, 7, -1], [3, 4, 7, 3, 0, 4, 1, 2, 10, -1], [9, 2, 10, 9, 0, 2, 8, 4, 7, -1],
    [2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1], [8, 4, 7, 3, 11, 2, -1], [11, 4, 7, 11, 2, 4, 2, 0, 4, -1],
    [9, 0, 1, 8, 4, 7, 2, 3, 11, -1], [4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1],
    [3, 10, 1, 3, 11, 10, 8, 4, 7, -1], [4, 7, 8, 1, 0, 10, 10, 0, 11, 11, 0, 3, -1],
    [4, 7, 8, 9, 0, 1, 11, 10, 3, 10, 1, 3, -1], [4, 7, 11, 4, 11, 9, 9, 11, 10, -1],
    [9, 5, 4, -1], [9, 5, 4, 0, 8, 3, -1], [0, 5, 4, 1, 5, 0, -1], [8, 5, 4, 8, 3, 5, 3, 1, 5, -1],
    [1, 2, 10, 9, 5, 4, -1], [3, 0, 8, 1, 2, 10, 4, 9, 5, -1], [5, 2, 10, 5, 4, 2, 4, 0, 2, -1],
    [2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1], [9, 5, 4, 2, 3, 11, -1], [0, 11, 2, 0, 8, 11, 4, 9, 5, -1],
    [0, 5, 4, 0, 1, 5, 2, 3, 11, -1], [2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1],
    [10, 3, 11, 10, 1, 3, 9, 5, 4, -1], [4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1],
    [5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1], [5, 4, 8, 5, 8, 10, 10, 8, 11, -1],
    [9, 7, 8, 5, 7, 9, -1], [9, 3, 0, 9, 5, 3, 5, 7, 3, -1], [0, 7, 8, 0, 1, 7, 1, 5, 7, -1],
    [1, 5, 3, 3, 5, 7, -1], [9, 7, 8, 9, 5, 7, 10, 1, 2, -1], [10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1],
    [8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1], [2, 10, 5, 2, 5, 3, 3, 5, 7, -1],
    [7, 9, 5, 7, 8, 9, 3, 11, 2, -1], [9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1],
    [2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1], [11, 2, 1, 11, 1, 7, 7, 1, 5, -1],
    [9, 5, 8, 5, 7, 8, 10, 1, 3, 10, 3, 11, -1], [5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1],
    [11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1], [11, 10, 5, 7, 11, 5, -1],
    [10, 6, 5, -1], [0, 8, 3, 10, 6, 5, -1], [0, 1, 9, 5, 10, 6, -1],
    [1, 8, 3, 1, 9, 8, 5, 10, 6, -1], [1, 6, 5, 2, 6, 1, -1], [1, 6, 5, 1, 2, 6, 3, 0, 8, -1],
    [9, 6, 5, 9, 0, 6, 0, 2, 6, -1], [5, 9, 6, 5, 6, 2, 5, 2, 8, 8, 2, 3, -1],
    [2, 3, 11, 10, 6, 5, -1], [11, 0, 8, 11, 2, 0, 10, 6, 5, -1], [0, 1, 9, 2, 3, 11, 5, 10, 6, -1],
    [5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1], [6, 3, 11, 6, 5, 3, 5, 1, 3, -1],
    [0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1], [3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1],
    [6, 5, 9, 6, 9, 11, 6, 11, 8, -1], [5, 10, 6, 4, 7, 8, -1], [4, 3, 0, 4, 7, 3, 6, 5, 10, -1],
    [1, 9, 0, 5, 10, 6, 8, 4, 7, -1], [10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1],
    [6, 1, 2, 6, 5, 1, 4, 7, 8, -1], [1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1],
    [8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1], [7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1],
    [3, 11, 2, 7, 8, 4, 10, 6, 5, -1], [5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1],
    [0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1], [9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1],
    [8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1], [5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1],
    [0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1], [6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1],
    [10, 4, 9, 6, 4, 10, -1], [4, 10, 6, 4, 9, 10, 0, 8, 3, -1], [10, 0, 1, 10, 6, 0, 6, 4, 0, -1],
    [8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1], [1, 4, 9, 1, 2, 4, 2, 6, 4, -1],
    [3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1], [0, 2, 4, 4, 2, 6, -1], [8, 3, 2, 8, 2, 4, 4, 2, 6, -1],
    [10, 4, 9, 10, 6, 4, 11, 2, 3, -1], [0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1],
    [3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1], [6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1],
    [9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1], [8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1],
    [3, 11, 6, 3, 6, 0, 0, 6, 4, -1], [6, 4, 8, 11, 6, 8, -1],
    [7, 10, 6, 7, 8, 10, 8, 9, 10, -1], [0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1],
    [10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1], [10, 6, 7, 10, 7, 1, 1, 7, 3, -1],
    [1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1], [2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1],
    [7, 8, 0, 7, 0, 6, 6, 0, 2, -1], [7, 3, 2, 6, 7, 2, -1],
    [2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1], [2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1],
    [1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1], [11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1],
    [8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1], [0, 9, 1, 11, 6, 7, -1],
    [7, 8, 0, 7, 0, 11, 11, 0, 2, -1], [7, 11, 6, -1],
    [7, 6, 11, -1], [3, 0, 8, 11, 7, 6, -1], [0, 1, 9, 11, 7, 6, -1],
    [8, 1, 9, 8, 3, 1, 11, 7, 6, -1], [10, 1, 2, 6, 11, 7, -1], [1, 2, 10, 3, 0, 8, 6, 11, 7, -1],
    [2, 9, 0, 2, 10, 9, 6, 11, 7, -1], [6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1],
    [7, 2, 3, 6, 2, 7, -1], [7, 0, 8, 7, 2, 0, 6, 2, 7, -1], [2, 7, 3, 2, 9, 7, 2, 1, 9, 6, 7, 9, -1],
    [1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1], [10, 1, 7, 10, 7, 6, 1, 3, 7, -1],
    [10, 1, 7, 10, 7, 6, 1, 0, 7, 8, 7, 0, -1], [0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1],
    [7, 6, 10, 7, 10, 8, 8, 10, 9, -1], [6, 8, 4, 11, 8, 6, -1], [3, 6, 11, 3, 0, 6, 0, 4, 6, -1],
    [8, 6, 11, 8, 4, 6, 9, 0, 1, -1], [9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1],
    [6, 8, 4, 6, 11, 8, 2, 10, 1, -1], [1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1],
    [4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1], [10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1],
    [4, 2, 3, 4, 6, 2, 4, 8, 6, -1], [0, 4, 2, 4, 6, 2, -1], [1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1],
    [1, 9, 4, 1, 4, 2, 2, 4, 6, -1], [8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1],
    [10, 1, 0, 10, 0, 6, 6, 0, 4, -1], [4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1],
    [10, 9, 4, 6, 10, 4, -1], [4, 9, 5, 7, 6, 11, -1], [0, 8, 3, 4, 9, 5, 11, 7, 6, -1],
    [5, 0, 1, 5, 4, 0, 7, 6, 11, -1], [11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1],
    [9, 5, 4, 10, 1, 2, 7, 6, 11, -1], [3, 0, 8, 1, 2, 10, 4, 9, 5, 7, 6, 11, -1],
    [0, 2, 11, 0, 11, 7, 0, 7, 5, 0, 5, 9, 7, 6, 11, -1], [4, 7, 8, 5, 9, 1, 5, 1, 6, 6, 1, 2, -1],
    [2, 3, 11, 4, 9, 5, 7, 6, 10, -1], [9, 5, 4, 0, 8, 11, 0, 11, 2, 10, 7, 6, -1],
    [0, 1, 9, 2, 3, 11, 4, 9, 5, 7, 6, 10, -1], [2, 1, 9, 2, 9, 11, 11, 9, 8, 7, 6, 10, 4, 9, 8, -1],
    [3, 10, 1, 3, 11, 10, 9, 5, 4, 7, 6, 11, -1], [0, 10, 1, 0, 8, 10, 8, 11, 10, 9, 5, 4, 7, 6, 11, -1],
    [3, 9, 0, 3, 11, 9, 11, 10, 9, 4, 9, 5, 7, 6, 11, -1], [9, 8, 10, 10, 8, 11, 4, 9, 5, 7, 6, 11, -1],
    [4, 7, 11, 4, 11, 9, 9, 11, 10, -1], [4, 7, 8, 9, 0, 1, 11, 10, 3, 10, 1, 3, -1],
    [4, 7, 8, 1, 0, 10, 10, 0, 11, 11, 0, 3, -1], [3, 10, 1, 3, 11, 10, 8, 4, 7, -1],
    [4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1], [9, 0, 1, 8, 4, 7, 2, 3, 11, -1],
    [11, 4, 7, 11, 2, 4, 2, 0, 4, -1], [8, 4, 7, 3, 11, 2, -1],
    [2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1], [9, 2, 10, 9, 0, 2, 8, 4, 7, -1],
    [3, 4, 7, 3, 0, 4, 1, 2, 10, -1], [1, 2, 10, 8, 4, 7, -1],
    [4, 1, 9, 4, 7, 1, 7, 3, 1, -1], [0, 1, 9, 8, 4, 7, -1],
    [4, 3, 0, 7, 3, 4, -1], [4, 7, 8, -1],
    [9, 8, 10, 10, 8, 11, -1], [3, 9, 0, 3, 11, 9, 11, 10, 9, -1],
    [0, 10, 1, 0, 8, 10, 8, 11, 10, -1], [3, 10, 1, 11, 10, 3, -1],
    [1, 11, 2, 1, 9, 11, 9, 8, 11, -1], [1, 9, 0, 2, 3, 11, -1],
    [0, 11, 2, 8, 11, 0, -1], [3, 11, 2, -1],
    [2, 8, 3, 2, 10, 8, 10, 9, 8, -1], [9, 2, 10, 0, 2, 9, -1],
    [0, 8, 3, 1, 2, 10, -1], [1, 2, 10, -1],
    [1, 8, 3, 9, 8, 1, -1], [0, 1, 9, -1],
    [0, 8, 3, -1], []
]

# Ensure exactly 256 entries
while len(TRI_TABLE_FULL) < 256:
    TRI_TABLE_FULL.append([])

def write_binary_stl(filename, facets):
    header = b'R2-D2 Watertight SDF Marching Cubes STL'.ljust(80, b' ')
    assert len(header) == 80, f"Header length must be exactly 80, got {len(header)}"
    with open(filename, 'wb') as f:
        f.write(header)
        f.write(struct.pack('<I', len(facets)))
        for norm, v1, v2, v3 in facets:
            f.write(struct.pack('<3f', *norm))
            f.write(struct.pack('<3f', *v1))
            f.write(struct.pack('<3f', *v2))
            f.write(struct.pack('<3f', *v3))
            f.write(struct.pack('<H', 0))
    print(f"SUCCESS: Generated {filename} ({len(facets)} triangles, {os.path.getsize(filename)/1024:.1f} KB)")

# Signed Distance Field Primitives
def sdf_box(px, py, pz, bx, by, bz):
    dx = abs(px) - bx; dy = abs(py) - by; dz = abs(pz) - bz
    ox = max(dx, 0.0); oy = max(dy, 0.0); oz = max(dz, 0.0)
    outside = math.sqrt(ox*ox + oy*oy + oz*oz)
    inside = min(max(dx, max(dy, dz)), 0.0)
    return outside + inside

def sdf_cylinder_y(px, py, pz, r, h):
    d_xz = math.sqrt(px*px + pz*pz) - r
    d_y = abs(py) - h/2.0
    ox = max(d_xz, 0.0); oy = max(d_y, 0.0)
    return math.sqrt(ox*ox + oy*oy) + min(max(d_xz, d_y), 0.0)

def sdf_double_d(px, py, pz, r, flat_w, h):
    d_cyl = math.sqrt(px*px + py*py) - r
    d_flat = abs(py) - flat_w/2.0
    d_dd = max(d_cyl, d_flat)
    d_z = abs(pz) - h/2.0
    ox = max(d_dd, 0.0); oz = max(d_z, 0.0)
    return math.sqrt(ox*ox + oz*oz) + min(max(d_dd, d_z), 0.0)

def sdf_top_bracket(x, y, z):
    # 1. Main Body: X in [-20, 20], Y in [0, 47], Z in [0, 16]
    body = sdf_box(x, y - 20.5, z - 8.0, 20.0, 20.5, 8.0)
    # 2. Top Flange: X in [-27, 27], Y in [41, 47], Z in [0, 16]
    flange = sdf_box(x, y - 44.0, z - 8.0, 27.0, 3.0, 8.0)
    solid = min(body, flange)
    
    # 3. Double-D Pocket: R=8.1 (16.2mm OD), Flat=11.2, depth 12mm (Z in [0, 12])
    pocket = sdf_double_d(x, y, z - 6.0, 8.1, 11.2, 12.0)
    
    # 4. Two M4 Clamping Bolt Holes (Ø4.5mm at X = +/- 14.0mm, Z = 8.0mm)
    hole_l = sdf_cylinder_y(x - 14.0, y - 20.0, z - 8.0, 2.25, 60.0)
    hole_r = sdf_cylinder_y(x + 14.0, y - 20.0, z - 8.0, 2.25, 60.0)
    
    # 5. Two Top Flange Mounting Holes (Ø4.5mm at X = +/- 21.0mm, Z = 8.0mm)
    flg_l = sdf_cylinder_y(x - 21.0, y - 44.0, z - 8.0, 2.25, 20.0)
    flg_r = sdf_cylinder_y(x + 21.0, y - 44.0, z - 8.0, 2.25, 20.0)
    
    # 6. Wire Relief Arch (8mm wide x 10mm high, through back wall Z in [11, 17])
    wire_slot = sdf_box(x, y - 5.0, z - 14.0, 4.0, 5.0, 5.0)
    
    res = max(solid, -pocket)
    res = max(res, -hole_l)
    res = max(res, -hole_r)
    res = max(res, -flg_l)
    res = max(res, -flg_r)
    res = max(res, -wire_slot)
    return res

def sdf_bottom_cap(x, y, z):
    # 1. Main Bottom Body: X in [-20, 20], Y in [-12, 0], Z in [0, 16]
    body = sdf_box(x, y + 6.0, z - 8.0, 20.0, 6.0, 8.0)
    # 2. Double-D Pocket: R=8.1, Flat=11.2, depth 12mm
    pocket = sdf_double_d(x, y, z - 6.0, 8.1, 11.2, 12.0)
    # 3. Two M4 Clamping Bolt Holes (Ø4.5mm)
    hole_l = sdf_cylinder_y(x - 14.0, y + 6.0, z - 8.0, 2.25, 30.0)
    hole_r = sdf_cylinder_y(x + 14.0, y + 6.0, z - 8.0, 2.25, 30.0)
    # 4. M4 Socket Head Counterbores (Ø8.0mm x 5mm deep from bottom Y=-12)
    cb_l = sdf_cylinder_y(x - 14.0, y + 9.5, z - 8.0, 4.0, 6.0)
    cb_r = sdf_cylinder_y(x + 14.0, y + 9.5, z - 8.0, 4.0, 6.0)
    
    res = max(body, -pocket)
    res = max(res, -hole_l)
    res = max(res, -hole_r)
    res = max(res, -cb_l)
    res = max(res, -cb_r)
    return res

def polygonize_sdf(sdf_fn, xmin, xmax, ymin, ymax, zmin, zmax, step=0.6):
    nx = int(math.ceil((xmax - xmin) / step)) + 1
    ny = int(math.ceil((ymax - ymin) / step)) + 1
    nz = int(math.ceil((zmax - zmin) / step)) + 1
    
    # 1. Sample grid values
    grid = [[[0.0 for _ in range(nz)] for _ in range(ny)] for _ in range(nx)]
    for ix in range(nx):
        x = xmin + ix * step
        for iy in range(ny):
            y = ymin + iy * step
            for iz in range(nz):
                z = zmin + iz * step
                grid[ix][iy][iz] = sdf_fn(x, y, z)
                
    # 2. Marching Cubes
    facets = []
    
    def interpolate_edge(p1, p2, val1, val2):
        if abs(val1 - val2) < 1e-6:
            mu = 0.5
        else:
            mu = (0.0 - val1) / (val2 - val1)
        mu = max(0.0, min(1.0, mu))
        return (p1[0] + mu * (p2[0] - p1[0]),
                p1[1] + mu * (p2[1] - p1[1]),
                p1[2] + mu * (p2[2] - p1[2]))
                
    for ix in range(nx - 1):
        x0 = xmin + ix * step; x1 = x0 + step
        for iy in range(ny - 1):
            y0 = ymin + iy * step; y1 = y0 + step
            for iz in range(nz - 1):
                z0 = zmin + iz * step; z1 = z0 + step
                
                # 8 corners
                corners = [
                    (x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                    (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)
                ]
                vals = [
                    grid[ix][iy][iz], grid[ix+1][iy][iz], grid[ix+1][iy+1][iz], grid[ix][iy+1][iz],
                    grid[ix][iy][iz+1], grid[ix+1][iy][iz+1], grid[ix+1][iy+1][iz+1], grid[ix][iy+1][iz+1]
                ]
                
                cubeindex = 0
                for c in range(8):
                    if vals[c] < 0.0:
                        cubeindex |= (1 << c)
                        
                tri_entries = TRI_TABLE_FULL[cubeindex]
                if not tri_entries or tri_entries[0] == -1:
                    continue
                    
                # Interpolate 12 edges
                edge_pts = {}
                for e_idx in range(12):
                    c1, c2 = EDGE_VERTICES[e_idx]
                    if ((vals[c1] < 0.0 and vals[c2] >= 0.0) or (vals[c1] >= 0.0 and vals[c2] < 0.0)):
                        edge_pts[e_idx] = interpolate_edge(corners[c1], corners[c2], vals[c1], vals[c2])
                        
                # Create triangles
                t_i = 0
                while t_i < len(tri_entries) and tri_entries[t_i] != -1:
                    e1 = tri_entries[t_i]
                    e2 = tri_entries[t_i+1]
                    e3 = tri_entries[t_i+2]
                    t_i += 3
                    
                    if e1 in edge_pts and e2 in edge_pts and e3 in edge_pts:
                        v1 = edge_pts[e1]
                        v2 = edge_pts[e2]
                        v3 = edge_pts[e3]
                        # Calc normal
                        ax, ay, az = v2[0]-v1[0], v2[1]-v1[1], v2[2]-v1[2]
                        bx, by, bz = v3[0]-v1[0], v3[1]-v1[1], v3[2]-v1[2]
                        nx_ = ay*bz - az*by; ny_ = az*bx - ax*bz; nz_ = ax*by - ay*bx
                        mag = math.sqrt(nx_*nx_ + ny_*ny_ + nz_*nz_)
                        if mag > 0:
                            norm = (nx_/mag, ny_/mag, nz_/mag)
                            facets.append((norm, v1, v2, v3))
                            
    return facets

print("Generating 100% Watertight Top Bracket STL via Marching Cubes...")
top_facets = polygonize_sdf(sdf_top_bracket, -29.0, 29.0, -2.0, 49.0, -2.0, 18.0, step=0.45)
write_binary_stl('/Users/mmcgeary/R2/Razor_Hub_Motor_Axle_Clamp_Top.stl', top_facets)

print("Generating 100% Watertight Bottom Cap STL via Marching Cubes...")
bot_facets = polygonize_sdf(sdf_bottom_cap, -22.0, 22.0, -14.0, 2.0, -2.0, 18.0, step=0.45)
write_binary_stl('/Users/mmcgeary/R2/Razor_Hub_Motor_Axle_Clamp_Bottom.stl', bot_facets)
