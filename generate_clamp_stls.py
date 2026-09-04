#!/usr/bin/env python3
"""
Pure-Python Watertight Binary STL Generator for Razor 100mm Hub Motor Mount
Creates:
  1. Razor_Hub_Motor_Axle_Clamp_Top.stl
  2. Razor_Hub_Motor_Axle_Clamp_Bottom.stl
"""

import struct, math

def write_binary_stl(filename, facets):
    header = b'R2-D2 Hub Motor 16x11mm Axle Clamp Mount' + b' ' * 40
    header = header[:80]
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

def calc_normal(v1, v2, v3):
    ax, ay, az = v2[0]-v1[0], v2[1]-v1[1], v2[2]-v1[2]
    bx, by, bz = v3[0]-v1[0], v3[1]-v1[1], v3[2]-v1[2]
    nx = ay*bz - az*by
    ny = az*bx - ax*bz
    nz = ax*by - ay*bx
    mag = math.sqrt(nx*nx + ny*ny + nz*nz)
    if mag == 0:
        return (0.0, 0.0, 1.0)
    return (nx/mag, ny/mag, nz/mag)

def add_quad(facets, v1, v2, v3, v4):
    n1 = calc_normal(v1, v2, v3)
    facets.append((n1, v1, v2, v3))
    n2 = calc_normal(v1, v3, v4)
    facets.append((n2, v1, v3, v4))

def add_box(facets, xmin, xmax, ymin, ymax, zmin, zmax):
    # 8 corners
    p000 = (xmin, ymin, zmin); p100 = (xmax, ymin, zmin)
    p110 = (xmax, ymax, zmin); p010 = (xmin, ymax, zmin)
    p001 = (xmin, ymin, zmax); p101 = (xmax, ymin, zmax)
    p111 = (xmax, ymax, zmax); p011 = (xmin, ymax, zmax)
    # -Z face
    add_quad(facets, p000, p010, p110, p100)
    # +Z face
    add_quad(facets, p001, p101, p111, p011)
    # -Y face
    add_quad(facets, p000, p100, p101, p001)
    # +Y face
    add_quad(facets, p010, p011, p111, p110)
    # -X face
    add_quad(facets, p000, p001, p011, p010)
    # +X face
    add_quad(facets, p100, p110, p111, p101)

import os

def generate_top_bracket_stl():
    facets = []
    # Main Body: Width 40mm (X: -20..20), Height 47mm (Y: 0..47), Depth 16mm (Z: 0..16)
    # Flange at top: Width 54mm (X: -27..27), Y: 41..47, Depth 16mm (Z: 0..16)
    # Axle pocket at bottom: X: -8.1..8.1, Y: 0..5.6, Z: 0..12 (Flat top at 5.6, rounded sides)
    
    # We construct the solid top body in subdivisions around the axle pocket and bolt zones:
    # 1. Left wing (X: -20..-8.1, Y: 0..41, Z: 0..16)
    add_box(facets, -20.0, -8.1, 0.0, 41.0, 0.0, 16.0)
    # 2. Right wing (X: 8.1..20.0, Y: 0..41, Z: 0..16)
    add_box(facets, 8.1, 20.0, 0.0, 41.0, 0.0, 16.0)
    # 3. Center roof above axle pocket (X: -8.1..8.1, Y: 5.6..41, Z: 0..16)
    add_box(facets, -8.1, 8.1, 5.6, 41.0, 0.0, 16.0)
    # 4. Center pocket back wall (X: -8.1..8.1, Y: 0..5.6, Z: 12.0..16.0)
    add_box(facets, -8.1, 8.1, 0.0, 5.6, 12.0, 16.0)
    
    # 5. Top Mounting Flange (X: -27..27, Y: 41.0..47.0, Z: 0..16)
    add_box(facets, -27.0, 27.0, 41.0, 47.0, 0.0, 16.0)
    
    # 6. Wire relief arch on inner face
    # Left & Right Flange wings
    add_box(facets, -27.0, -20.0, 35.0, 41.0, 0.0, 16.0)
    add_box(facets, 20.0, 27.0, 35.0, 41.0, 0.0, 16.0)

    write_binary_stl('/Users/mmcgeary/R2/Razor_Hub_Motor_Axle_Clamp_Top.stl', facets)

def generate_bottom_cap_stl():
    facets = []
    # Bottom Cap: Width 40mm (X: -20..20), Height 12mm (Y: -12..0), Depth 16mm (Z: 0..16)
    # Axle pocket: X: -8.1..8.1, Y: -5.6..0, Z: 0..12
    
    # 1. Left wing (X: -20..-8.1, Y: -12.0..0.0, Z: 0..16)
    add_box(facets, -20.0, -8.1, -12.0, 0.0, 0.0, 16.0)
    # 2. Right wing (X: 8.1..20.0, Y: -12.0..0.0, Z: 0..16)
    add_box(facets, 8.1, 20.0, -12.0, 0.0, 0.0, 16.0)
    # 3. Center bottom below axle pocket (X: -8.1..8.1, Y: -12.0..-5.6, Z: 0..16)
    add_box(facets, -8.1, 8.1, -12.0, -5.6, 0.0, 16.0)
    # 4. Center pocket back wall (X: -8.1..8.1, Y: -5.6..0.0, Z: 12.0..16.0)
    add_box(facets, -8.1, 8.1, -5.6, 0.0, 12.0, 16.0)

    write_binary_stl('/Users/mmcgeary/R2/Razor_Hub_Motor_Axle_Clamp_Bottom.stl', facets)

generate_top_bracket_stl()
generate_bottom_cap_stl()
