import math, struct, subprocess

def write_binary_stl(filename, verts_3d, tris_3d):
    header = b'R2-D2 Cradle Axle Bottom Keeper Plate'.ljust(80, b' ')
    with open(filename, 'wb') as f:
        f.write(header)
        f.write(struct.pack('<I', len(tris_3d)))
        for i1, i2, i3 in tris_3d:
            p1 = verts_3d[i1]; p2 = verts_3d[i2]; p3 = verts_3d[i3]
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

# Simple 16mm x 32.5mm x 12mm locking insert block
# X: -8.0 to +8.0 (16mm)
# Y: 0 to 32.5mm
# Z: 0 to 12.0mm
verts = [
    (-8.0, 0.0, 0.0), (8.0, 0.0, 0.0), (8.0, 32.5, 0.0), (-8.0, 32.5, 0.0),
    (-8.0, 0.0, 12.0), (8.0, 0.0, 12.0), (8.0, 32.5, 12.0), (-8.0, 32.5, 12.0)
]
tris = [
    # -Z face
    (0, 2, 1), (0, 3, 2),
    # +Z face
    (4, 5, 6), (4, 6, 7),
    # -Y face
    (0, 1, 5), (0, 5, 4),
    # +Y face (touches bottom 11mm flat of axle!)
    (2, 3, 7), (2, 7, 6),
    # -X face
    (3, 0, 4), (3, 4, 7),
    # +X face
    (1, 2, 6), (1, 6, 5)
]

out_k = '/Users/mmcgeary/R2/Razor_Hub_Motor_Bottom_Keeper.stl'
write_binary_stl(out_k, verts, tris)

out = subprocess.check_output(['/Applications/BambuStudio.app/Contents/MacOS/BambuStudio', '--info', out_k]).decode()
print(out)
