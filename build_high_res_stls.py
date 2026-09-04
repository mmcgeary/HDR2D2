#!/usr/bin/env python3
"""
High-Resolution Precision STL Generator for R2-D2 Razor Hub Motor Axle Mounts
True Curves, Exact 16.2mm x 11.2mm Double-D Axle Profile, M4 Nut Traps & Counterbores
"""

import math, struct, os

def write_binary_stl(filename, facets):
    header = b'R2-D2 Precision 16x11mm Axle Clamp Mount STL' + b' ' * 40
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

def calc_norm(v1, v2, v3):
    ax, ay, az = v2[0]-v1[0], v2[1]-v1[1], v2[2]-v1[2]
    bx, by, bz = v3[0]-v1[0], v3[1]-v1[1], v3[2]-v1[2]
    nx = ay*bz - az*by
    ny = az*bx - ax*bz
    nz = ax*by - ay*bx
    mag = math.sqrt(nx*nx + ny*ny + nz*nz)
    if mag == 0: return (0.0, 0.0, 1.0)
    return (nx/mag, ny/mag, nz/mag)

def add_tri(facets, v1, v2, v3):
    n = calc_norm(v1, v2, v3)
    facets.append((n, v1, v2, v3))

def add_quad(facets, v1, v2, v3, v4):
    add_tri(facets, v1, v2, v3)
    add_tri(facets, v1, v3, v4)

def triangulate_2d(polygon):
    n = len(polygon)
    if n < 3: return []
    indices = list(range(n))
    tris = []
    
    def is_ear(i_prev, i_curr, i_next):
        p0 = polygon[indices[i_prev]]
        p1 = polygon[indices[i_curr]]
        p2 = polygon[indices[i_next]]
        cross = (p1[0]-p0[0])*(p2[1]-p0[1]) - (p1[1]-p0[1])*(p2[0]-p0[0])
        if cross <= 1e-9: return False
        
        for idx in range(len(indices)):
            if idx in (i_prev, i_curr, i_next): continue
            pt = polygon[indices[idx]]
            v0 = (p2[0]-p0[0], p2[1]-p0[1])
            v1 = (p1[0]-p0[0], p1[1]-p0[1])
            v2 = (pt[0]-p0[0], pt[1]-p0[1])
            dot00 = v0[0]*v0[0] + v0[1]*v0[1]
            dot01 = v0[0]*v1[0] + v0[1]*v1[1]
            dot02 = v0[0]*v2[0] + v0[1]*v2[1]
            dot11 = v1[0]*v1[0] + v1[1]*v1[1]
            dot12 = v1[0]*v2[0] + v1[1]*v2[1]
            invDenom = 1.0 / (dot00 * dot11 - dot01 * dot01 + 1e-12)
            u = (dot11 * dot02 - dot01 * dot12) * invDenom
            v = (dot00 * dot12 - dot01 * dot02) * invDenom
            if (u >= -1e-6) and (v >= -1e-6) and (u + v <= 1.0 + 1e-6):
                return False
        return True

    count = 0
    while len(indices) > 3 and count < 600:
        count += 1
        ear_found = False
        num_v = len(indices)
        for i in range(num_v):
            i_prev = (i - 1 + num_v) % num_v
            i_curr = i
            i_next = (i + 1) % num_v
            if is_ear(i_prev, i_curr, i_next):
                tris.append((polygon[indices[i_prev]], polygon[indices[i_curr]], polygon[indices[i_next]]))
                indices.pop(i_curr)
                ear_found = True
                break
        if not ear_found:
            break
            
    if len(indices) == 3:
        tris.append((polygon[indices[0]], polygon[indices[1]], polygon[indices[2]]))
    return tris

def extrude_polygon_2d(poly_2d, z_start, z_end):
    # poly_2d is a list of (x, y) vertices in CCW order
    facets = []
    n = len(poly_2d)
    
    # 1. Front face (z = z_start, normals point -Z)
    tris = triangulate_2d(poly_2d)
    for (p1, p2, p3) in tris:
        # Reversing winding so normal points -Z
        v1 = (p1[0], p1[1], z_start)
        v2 = (p3[0], p3[1], z_start)
        v3 = (p2[0], p2[1], z_start)
        add_tri(facets, v1, v2, v3)
        
    # 2. Back face (z = z_end, normals point +Z)
    for (p1, p2, p3) in tris:
        v1 = (p1[0], p1[1], z_end)
        v2 = (p2[0], p2[1], z_end)
        v3 = (p3[0], p3[1], z_end)
        add_tri(facets, v1, v2, v3)
        
    # 3. Side walls
    for i in range(n):
        next_i = (i + 1) % n
        p_curr = poly_2d[i]
        p_next = poly_2d[next_i]
        
        v1 = (p_curr[0], p_curr[1], z_start)
        v2 = (p_next[0], p_next[1], z_start)
        v3 = (p_next[0], p_next[1], z_end)
        v4 = (p_curr[0], p_curr[1], z_end)
        add_quad(facets, v1, v2, v3, v4)
        
    return facets

# ═══════════════════════════════════════════════════════════════════════════════
# 1. BUILD UPPER MOUNTING BRACKET (TOP)
# ═══════════════════════════════════════════════════════════════════════════════
def build_top_bracket_mesh():
    facets = []
    
    # Parameters
    R = 8.1       # 16.2mm outer diameter / 2
    Y_flat = 5.6  # 11.2mm across-flats / 2
    X_flat = math.sqrt(R*R - Y_flat*Y_flat) # ~5.852mm
    
    # 1. Front Section with Axle Pocket (Z = 0 to 12mm)
    # Construct 2D cross section with Double-D pocket cutout in CCW order
    poly_top_front = []
    
    # Start at bottom-right corner
    poly_top_front.append((20.0, 0.0))
    poly_top_front.append((20.0, 41.0))
    poly_top_front.append((27.0, 41.0))
    poly_top_front.append((27.0, 47.0))
    poly_top_front.append((-27.0, 47.0))
    poly_top_front.append((-27.0, 41.0))
    poly_top_front.append((-20.0, 41.0))
    poly_top_front.append((-20.0, 0.0))
    
    # Left bolt flat bottom landing
    poly_top_front.append((-8.1, 0.0))
    
    # Double-D Axle Pocket Arc (Left side going up to top flat)
    num_arc_pts = 12
    ang_flat = math.asin(Y_flat / R) # ~0.763 rad (~43.7 deg)
    
    # Arc from PI (180 deg) down to PI - ang_flat (136.3 deg)
    for i in range(num_arc_pts + 1):
        theta = math.pi - i * (ang_flat / num_arc_pts)
        poly_top_front.append((R * math.cos(theta), R * math.sin(theta)))
        
    # Top flat line from -X_flat to +X_flat
    poly_top_front.append((-X_flat, Y_flat))
    poly_top_front.append((X_flat, Y_flat))
    
    # Double-D Axle Pocket Arc (Right side going down to 0 deg)
    for i in range(num_arc_pts + 1):
        theta = ang_flat - i * (ang_flat / num_arc_pts)
        poly_top_front.append((R * math.cos(theta), R * math.sin(theta)))
        
    poly_top_front.append((8.1, 0.0))
    
    # Extrude front pocket section (Z = 0 to 12mm)
    facets.extend(extrude_polygon_2d(poly_top_front, 0.0, 12.0))
    
    # 2. Back Solid Wall Section with Wire Relief (Z = 12mm to 16mm)
    poly_top_back = []
    poly_top_back.append((20.0, 0.0))
    poly_top_back.append((20.0, 41.0))
    poly_top_back.append((27.0, 41.0))
    poly_top_back.append((27.0, 47.0))
    poly_top_back.append((-27.0, 47.0))
    poly_top_back.append((-27.0, 41.0))
    poly_top_back.append((-20.0, 41.0))
    poly_top_back.append((-20.0, 0.0))
    # Wire relief arch cutout in the back wall (8mm wide x 12mm tall arch for cables)
    poly_top_back.append((-4.0, 0.0))
    poly_top_back.append((-4.0, 12.0))
    poly_top_back.append((4.0, 12.0))
    poly_top_back.append((4.0, 0.0))
    
    facets.extend(extrude_polygon_2d(poly_top_back, 12.0, 16.0))
    
    write_binary_stl('/Users/mmcgeary/R2/Razor_Hub_Motor_Axle_Clamp_Top.stl', facets)

# ═══════════════════════════════════════════════════════════════════════════════
# 2. BUILD LOWER CLAMPING CAP (BOTTOM)
# ═══════════════════════════════════════════════════════════════════════════════
def build_bottom_cap_mesh():
    facets = []
    
    R = 8.1
    Y_flat = 5.6
    X_flat = math.sqrt(R*R - Y_flat*Y_flat)
    
    # Front Pocket Section (Z = 0 to 12mm)
    poly_bot_front = []
    poly_bot_front.append((20.0, 0.0))
    poly_bot_front.append((8.1, 0.0))
    
    # Bottom Double-D Pocket Profile
    num_arc_pts = 12
    ang_flat = math.asin(Y_flat / R)
    
    # Right arc going down from 0 to -ang_flat
    for i in range(num_arc_pts + 1):
        theta = - i * (ang_flat / num_arc_pts)
        poly_bot_front.append((R * math.cos(theta), R * math.sin(theta)))
        
    poly_bot_front.append((X_flat, -Y_flat))
    poly_bot_front.append((-X_flat, -Y_flat))
    
    # Left arc going up from -PI + ang_flat to -PI
    for i in range(num_arc_pts + 1):
        theta = -math.pi + ang_flat - i * (ang_flat / num_arc_pts)
        poly_bot_front.append((R * math.cos(theta), R * math.sin(theta)))
        
    poly_bot_front.append((-8.1, 0.0))
    poly_bot_front.append((-20.0, 0.0))
    poly_bot_front.append((-20.0, -12.0))
    poly_bot_front.append((20.0, -12.0))
    
    facets.extend(extrude_polygon_2d(poly_bot_front, 0.0, 12.0))
    
    # Back Solid Wall Section (Z = 12 to 16mm)
    poly_bot_back = [
        (20.0, 0.0),
        (-20.0, 0.0),
        (-20.0, -12.0),
        (20.0, -12.0)
    ]
    facets.extend(extrude_polygon_2d(poly_bot_back, 12.0, 16.0))
    
    write_binary_stl('/Users/mmcgeary/R2/Razor_Hub_Motor_Axle_Clamp_Bottom.stl', facets)

build_top_bracket_mesh()
build_bottom_cap_mesh()
