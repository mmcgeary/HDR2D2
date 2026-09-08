#!/usr/bin/env python3
"""
Updated 120mm Drop-in Motor Carriage for Razor 100mm Hub Motor
Engineered for D-Axle Shaft: 16mm Long (Horizontal) x 11mm Tall (Flat Top/Bottom)
Verified with BambuStudio CLI
"""

import math, struct, subprocess, os

def generate_cradle_stl(filename):
    # Axle parameters:
    # Length (horizontal in X): 16.0mm (rounded front/back) -> slot width in X = 16.2mm (X: -8.1 to +8.1)
    # Height (vertical in Y): 11.0mm (flat top/bottom) -> axle sits from Y=32.5 to Y=43.5, center at Y=38.0
    #
    # X Grid:
    # -60.0: rear edge
    # -52.0: rear bulkhead inside edge (8mm solid rear drilling platform)
    # -22.0: rear washer pocket start
    #  -8.1: rear slot wall (16.2mm slot width)
    #   8.1: front slot wall (16.2mm slot width)
    #  22.0: front washer pocket end
    #  52.0: front bulkhead inside edge (8mm solid front drilling platform)
    #  60.0: front edge
    X = [-60.0, -52.0, -22.0, -8.1, 8.1, 22.0, 52.0, 60.0]

    # Y Grid:
    #  0.0: bottom skirt edge
    # 32.5: bottom flat of axle
    # 38.0: axle centerline (gives 12mm ground clearance on 100mm wheel)
    # 43.5: top flat of axle / pocket ceiling
    # 65.0: top deck platform lower boundary
    # 75.0: top deck platform upper boundary
    Y = [0.0, 32.5, 38.0, 43.5, 65.0, 75.0]

    # Z Grid:
    # -41.0: outer left edge (slips into 84mm shell with 1mm clearance)
    # -33.0: outer face of left slotted wall (8mm pocket for washer/bolt)
    # -21.0: inner face of left slotted wall (12mm thick slotted wall)
    #  21.0: inner face of right slotted wall (42mm central wheel cavity)
    #  33.0: outer face of right slotted wall (12mm thick slotted wall)
    #  41.0: outer right edge (8mm pocket for washer/bolt)
    Z = [-41.0, -33.0, -21.0, 21.0, 33.0, 41.0]

    nx = len(X) - 1
    ny = len(Y) - 1
    nz = len(Z) - 1

    solid = [[[False for _ in range(nz)] for _ in range(ny)] for _ in range(nx)]

    for ix in range(nx):
        for iy in range(ny):
            for iz in range(nz):

                # 1. 16.2mm Axle Dropout Slot: ALWAYS AIR
                # Between X = -8.1 and +8.1 (ix == 3), for Y <= 43.5mm (iy in 0, 1, 2)
                if ix == 3 and iy in (0, 1, 2):
                    solid[ix][iy][iz] = False
                    continue

                # 2. Central Wheel Cavity (Z in [-21, 21] -> iz == 2)
                # The 100mm wheel sits in X in [-52, 52] (ix in 1, 2, 3, 4, 5)
                # Must be completely open so wheel spins freely!
                if iz == 2 and ix in (1, 2, 3, 4, 5):
                    solid[ix][iy][iz] = False
                    continue

                # 3. Outer Washer / Screw Pockets (iz == 0 or iz == 4)
                # In the axle zone (ix in 2, 3, 4 -> X in [-22, 22]),
                # leave an 8mm recess on the outside for the fender washer & screw head
                if iz in (0, 4) and ix in (2, 3, 4) and iy in (0, 1, 2):
                    solid[ix][iy][iz] = False
                    continue

                # 4. Front and Rear Bulkheads / Drilling Platforms (ix == 0 or ix == 6):
                # Solid across the full 82mm width from bottom to top!
                if ix in (0, 6):
                    solid[ix][iy][iz] = True
                    continue

                # 5. Top Deck Bridges (iy == 4 -> Y in [65, 75]):
                # Outer side beams along Z in [-41, -21] and [21, 41]
                if iy == 4 and iz != 2:
                    solid[ix][iy][iz] = True
                    continue

                # 6. Slotted Side Walls (iz in 1, 3):
                # Left slotted wall is iz == 1 (Z: -33 to -21)
                # Right slotted wall is iz == 3 (Z: 21 to 33)
                if iz in (1, 3):
                    solid[ix][iy][iz] = True
                    continue

                # 7. Outer Side Flanges outside the washer pockets:
                if iz in (0, 4) and ix in (1, 5):
                    solid[ix][iy][iz] = True
                    continue

                # 8. Solid bridge above the axle slot (ix == 3, iy in 3, 4):
                if ix == 3 and iy >= 3 and iz != 2:
                    solid[ix][iy][iz] = True
                    continue

    # Extract watertight boundary quads
    verts = []
    vert_map = {}
    tris = []

    def get_v(x, y, z):
        k = (round(x, 4), round(y, 4), round(z, 4))
        if k not in vert_map:
            idx = len(verts)
            verts.append((x, y, z))
            vert_map[k] = idx
            return idx
        return vert_map[k]

    def add_quad(p1, p2, p3, p4):
        i1 = get_v(*p1); i2 = get_v(*p2); i3 = get_v(*p3); i4 = get_v(*p4)
        tris.append((i1, i2, i3))
        tris.append((i1, i3, i4))

    for ix in range(nx):
        x0, x1 = X[ix], X[ix+1]
        for iy in range(ny):
            y0, y1 = Y[iy], Y[iy+1]
            for iz in range(nz):
                z0, z1 = Z[iz], Z[iz+1]

                if not solid[ix][iy][iz]:
                    continue

                # -X face
                if ix == 0 or not solid[ix-1][iy][iz]:
                    add_quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0))
                # +X face
                if ix == nx - 1 or not solid[ix+1][iy][iz]:
                    add_quad((x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1))
                # -Y face
                if iy == 0 or not solid[ix][iy-1][iz]:
                    add_quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1))
                # +Y face
                if iy == ny - 1 or not solid[ix][iy+1][iz]:
                    add_quad((x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0))
                # -Z face
                if iz == 0 or not solid[ix][iy][iz-1]:
                    add_quad((x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0))
                # +Z face
                if iz == nz - 1 or not solid[ix][iy][iz+1]:
                    add_quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))

    header = b'R2-D2 120mm Cradle (16x11mm Axle Slot)'.ljust(80, b' ')
    with open(filename, 'wb') as f:
        f.write(header)
        f.write(struct.pack('<I', len(tris)))
        for i1, i2, i3 in tris:
            p1 = verts[i1]; p2 = verts[i2]; p3 = verts[i3]
            ax, ay, az = p2[0]-p1[0], p2[1]-p1[1], p2[2]-p1[2]
            bx, by, bz = p3[0]-p1[0], p3[1]-p1[1], p3[2]-p1[2]
            nx_ = ay*bz - az*by; ny_ = az*bx - ax*bz; nz_ = ax*by - ay*bx
            mag = math.sqrt(nx_*nx_ + ny_*ny_ + nz_*nz_)
            norm = (nx_/mag, ny_/mag, nz_/mag) if mag > 0 else (0.0, 0.0, 0.0)
            f.write(struct.pack('<3f', *norm))
            f.write(struct.pack('<3f', *p1))
            f.write(struct.pack('<3f', *p2))
            f.write(struct.pack('<3f', *p3))
            f.write(struct.pack('<H', 0))

    print(f"Generated {filename}: {len(verts)} vertices, {len(tris)} triangles")

out_stl = '/Users/mmcgeary/R2/Razor_Hub_Motor_120mm_Cradle.stl'
generate_cradle_stl(out_stl)

# Verify with BambuStudio CLI!
print("\n--- BAMBU STUDIO SLICER VERIFICATION ---")
bambu_out = subprocess.check_output(['/Applications/BambuStudio.app/Contents/MacOS/BambuStudio', '--info', out_stl]).decode()
print(bambu_out)
