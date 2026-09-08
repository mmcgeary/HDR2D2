import math, struct, subprocess

def write_binary_stl(filename, verts_3d, tris_3d):
    header = b'R2-D2 Precision PETG Slotted Mount'.ljust(80, b' ')
    with open(filename, 'wb') as f:
        f.write(header)
        f.write(struct.pack('<I', len(tris_3d)))
        for i1, i2, i3 in tris_3d:
            p1 = verts_3d[i1]; p2 = verts_3d[i2]; p3 = verts_3d[i3]
            ax, ay, az = p2[0]-p1[0], p2[1]-p1[1], p2[2]-p1[2]
            bx, by, bz = p3[0]-p1[0], p3[1]-p1[1], p3[2]-p1[2]
            nx = ay*bz - az*by; ny = az*bx - ax*bz; nz = ax*by - ay*bx
            mag = math.sqrt(nx*nx + ny*ny + nz*nz)
            norm = (nx/mag, ny/mag, nz/mag) if mag > 0 else (0.0, 0.0, 0.0)
            f.write(struct.pack('<3f', *norm))
            f.write(struct.pack('<3f', *p1))
            f.write(struct.pack('<3f', *p2))
            f.write(struct.pack('<3f', *p3))
            f.write(struct.pack('<H', 0))

def extrude_2d_mesh(verts_2d, tris_2d, thickness):
    N = len(verts_2d)
    verts_3d = [(x, y, 0.0) for x, y in verts_2d] + [(x, y, thickness) for x, y in verts_2d]
    tris_3d = []
    # Bottom face (normals -Z)
    for i1, i2, i3 in tris_2d:
        tris_3d.append((i1, i3, i2))
    # Top face (normals +Z)
    for i1, i2, i3 in tris_2d:
        tris_3d.append((i1 + N, i2 + N, i3 + N))

    edge_counts = {}
    for i1, i2, i3 in tris_2d:
        for a, b in [(i1, i2), (i2, i3), (i3, i1)]:
            edge_counts[(a, b)] = edge_counts.get((a, b), 0) + 1

    boundary_edges = []
    for (a, b), count in edge_counts.items():
        if (b, a) not in edge_counts:
            boundary_edges.append((a, b))

    for a, b in boundary_edges:
        v1, v2, v3, v4 = a, b, b + N, a + N
        tris_3d.append((v1, v2, v3))
        tris_3d.append((v1, v3, v4))

    return verts_3d, tris_3d

class GridMesh2D:
    def __init__(self):
        self.verts = []
        self.tris = []
        self.vert_map = {}

    def get_v(self, x, y):
        k = (round(x, 4), round(y, 4))
        if k not in self.vert_map:
            idx = len(self.verts)
            self.verts.append((x, y))
            self.vert_map[k] = idx
            return idx
        return self.vert_map[k]

    def add_quad(self, p0, p1, p2, p3):
        # p0, p1, p2, p3 CCW
        i0 = self.get_v(*p0); i1 = self.get_v(*p1)
        i2 = self.get_v(*p2); i3 = self.get_v(*p3)
        self.tris.append((i0, i1, i2))
        self.tris.append((i0, i2, i3))

    def add_cell_with_hole(self, x0, x1, y0, y1, cx, cy, r):
        # 8 boundary points on rectangle
        xm = (x0 + x1) / 2.0; ym = (y0 + y1) / 2.0
        outer = [
            (x0, y0), (xm, y0), (x1, y0),
            (x1, ym), (x1, y1), (xm, y1),
            (x0, y1), (x0, ym)
        ]
        angles = [
            -3*math.pi/4, -math.pi/2, -math.pi/4,
            0, math.pi/4, math.pi/2, 3*math.pi/4, math.pi
        ]
        inner = [(cx + r * math.cos(a), cy + r * math.sin(a)) for a in angles]
        for i in range(8):
            ni = (i + 1) % 8
            self.add_quad(outer[i], outer[ni], inner[ni], inner[i])

# Let's define the grid:
# Holes are 20mm x 26mm cells centered at (-20, 13), (+20, 13), (-20, 54.5), (+20, 54.5)
# Slot is 11.5mm wide (-5.75 to +5.75)
X = [-30.0, -20.0, -10.0, -5.75, 5.75, 10.0, 20.0, 30.0]
Y = [0.0, 13.0, 26.0, 38.0, 44.0, 54.5, 65.0]

mesh = GridMesh2D()

# Lower Holes:
# Left lower hole in [-30, -10] x [0, 26], hole at (-20, 13)
mesh.add_cell_with_hole(-30.0, -10.0, 0.0, 26.0, -20.0, 13.0, 2.25)
# Right lower hole in [10, 30] x [0, 26], hole at (+20, 13)
mesh.add_cell_with_hole(10.0, 30.0, 0.0, 26.0, 20.0, 13.0, 2.25)

# Left slot lower wall: [-10, -5.75] in Y=[0, 13] and Y=[13, 26]
mesh.add_quad((-10.0, 0.0), (-5.75, 0.0), (-5.75, 13.0), (-10.0, 13.0))
mesh.add_quad((-10.0, 13.0), (-5.75, 13.0), (-5.75, 26.0), (-10.0, 26.0))

# Right slot lower wall: [5.75, 10] in Y=[0, 13] and Y=[13, 26]
mesh.add_quad((5.75, 0.0), (10.0, 0.0), (10.0, 13.0), (5.75, 13.0))
mesh.add_quad((5.75, 13.0), (10.0, 13.0), (10.0, 26.0), (5.75, 26.0))

# Mid section Y in [26, 38]:
# Left pillar: [-30, -5.75] with vertices matching X=[-30, -20, -10, -5.75]
for i in range(3):
    x_a, x_b = X[i], X[i+1]
    mesh.add_quad((x_a, 26.0), (x_b, 26.0), (x_b, 38.0), (x_a, 38.0))

# Right pillar: [5.75, 30] with vertices matching X=[5.75, 10, 20, 30]
for i in range(4, 7):
    x_a, x_b = X[i], X[i+1]
    mesh.add_quad((x_a, 26.0), (x_b, 26.0), (x_b, 38.0), (x_a, 38.0))

# Crown Section Y in [38, 44]:
# Left side of crown: X in [-30, -5.75]
for i in range(3):
    x_a, x_b = X[i], X[i+1]
    mesh.add_quad((x_a, 38.0), (x_b, 38.0), (x_b, 44.0), (x_a, 44.0))

# Right side of crown: X in [5.75, 30]
for i in range(4, 7):
    x_a, x_b = X[i], X[i+1]
    mesh.add_quad((x_a, 38.0), (x_b, 38.0), (x_b, 44.0), (x_a, 44.0))

# Semicircular Crown Arch between (5.75, 38) and (-5.75, 38):
# 6 arc segments
n_crown = 6
crown_pts = []
for i in range(n_crown + 1):
    ang = i * math.pi / n_crown
    cx = 5.75 * math.cos(ang)
    cy = 38.0 + 5.75 * math.sin(ang)
    crown_pts.append((cx, cy))

for i in range(n_crown):
    p_arc1 = crown_pts[i]
    p_arc2 = crown_pts[i+1]
    c_x1 = 5.75 - i * (11.5 / n_crown)
    c_x2 = 5.75 - (i + 1) * (11.5 / n_crown)
    mesh.add_quad(p_arc1, (c_x1, 44.0), (c_x2, 44.0), p_arc2)

# Upper Holes:
# Left upper hole in [-30, -10] x [44, 65], hole at (-20, 54.5)
mesh.add_cell_with_hole(-30.0, -10.0, 44.0, 65.0, -20.0, 54.5, 2.25)
# Right upper hole in [10, 30] x [44, 65], hole at (+20, 54.5)
mesh.add_cell_with_hole(10.0, 30.0, 44.0, 65.0, 20.0, 54.5, 2.25)

# Center roof in [-10, 10] x [44, 65]
# Break into sub-cells matching X=[-10, -5.75, 5.75, 10] and Y=[44, 54.5, 65]
roof_X = [-10.0, -5.75, 5.75, 10.0]
roof_Y = [44.0, 54.5, 65.0]
for ix in range(len(roof_X) - 1):
    for iy in range(len(roof_Y) - 1):
        mesh.add_quad((roof_X[ix], roof_Y[iy]),
                      (roof_X[ix+1], roof_Y[iy]),
                      (roof_X[ix+1], roof_Y[iy+1]),
                      (roof_X[ix], roof_Y[iy+1]))

verts_3d, tris_3d = extrude_2d_mesh(mesh.verts, mesh.tris, 12.0)
out_file = '/Users/mmcgeary/R2/Razor_Hub_Motor_Slotted_Mount.stl'
write_binary_stl(out_file, verts_3d, tris_3d)

out = subprocess.check_output(['/Applications/BambuStudio.app/Contents/MacOS/BambuStudio', '--info', out_file]).decode()
print("\n" + out)
