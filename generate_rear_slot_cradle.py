import math, struct, subprocess

# Let's add the semicircular cap into the 3D grid generator!
# The slot runs from X = -60 to X = 0.
# The semicircular cap extends from X = 0 forward to X = 5.6:
# We add intermediate X divisions in [0, 5.6]:
# X = [..., -20.0, 0.0, 3.96, 5.6, 20.0, ...]
# And intermediate Y divisions around Y=38.0 (radius 5.6):
# At X = 3.96, the half-height is sqrt(5.6^2 - 3.96^2) = sqrt(31.36 - 15.68) = sqrt(15.68) = 3.96!
# So Y goes from 38 - 3.96 = 34.04 to 38 + 3.96 = 41.96!
# This creates an exact, beautiful octagonal / curved semicircular cap!

X = [-60.0, -50.0, -20.0, 0.0, 3.96, 5.6, 20.0, 50.0, 60.0]
Y = [0.0, 32.4, 34.04, 38.0, 41.96, 43.6, 65.0, 75.0]
Z = [-41.0, -33.0, -21.0, 21.0, 33.0, 41.0]

nx = len(X) - 1
ny = len(Y) - 1
nz = len(Z) - 1

solid = [[[False for _ in range(nz)] for _ in range(ny)] for _ in range(nx)]

for ix in range(nx):
    for iy in range(ny):
        for iz in range(nz):
            # 1. Horizontal slot from rear (X: -60 to 0 -> ix in 0, 1, 2):
            # Height Y in [32.4, 43.6] -> iy in (1, 2, 3, 4)
            if ix in (0, 1, 2) and iy in (1, 2, 3, 4):
                solid[ix][iy][iz] = False
                continue

            # 2. Semicircular Cap Zone (ix in 3, 4 -> X in [0, 5.6]):
            # At ix == 3 (X in [0, 3.96]): air in Y in [34.04, 41.96] (iy in 2, 3)
            if ix == 3 and iy in (2, 3):
                solid[ix][iy][iz] = False
                continue

            # 3. Central Wheel Cavity (iz == 2):
            # Wheel sits in X in [-50, 50] (ix in 1..6)
            if iz == 2 and ix in (1, 2, 3, 4, 5, 6):
                solid[ix][iy][iz] = False
                continue

            # 4. Outer Washer Pockets (iz in 0, 4):
            # In axle zone (ix in 2, 3, 4, 5 -> X in [-20, 20])
            if iz in (0, 4) and ix in (2, 3, 4, 5) and iy in (0, 1, 2, 3, 4, 5):
                solid[ix][iy][iz] = False
                continue

            # 5. Front Solid Bulkhead (ix == 7 -> X in [50, 60]):
            if ix == 7:
                solid[ix][iy][iz] = True
                continue

            # 6. Top Deck Bridges (iy == 6 -> Y in [65, 75]):
            if iy == 6 and iz != 2:
                solid[ix][iy][iz] = True
                continue

            # 7. Slotted Side Walls (iz in 1, 3):
            if iz in (1, 3):
                solid[ix][iy][iz] = True
                continue

            # 8. Outer Side Flanges (iz in 0, 4 outside washer pocket):
            if iz in (0, 4) and ix in (0, 1, 6):
                solid[ix][iy][iz] = True
                continue

            # 9. Rear Upper Cross-Beam (ix == 0, iy in 5, 6):
            if ix == 0 and iy in (5, 6):
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

header = b'R2-D2 Rear Slot Semicircular Cap Cradle'.ljust(80, b' ')
filename = '/tmp/test_curved_cap_cradle.stl'
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

out = subprocess.check_output(['/Applications/BambuStudio.app/Contents/MacOS/BambuStudio', '--info', filename]).decode()
print(out)
