"""Writes plate and lettering (or, with --knoepfe, ten knobs of body and pointer) as 3MF objects of two parts each,
so the slicer keeps the parts where they belong."""
import struct, sys, zipfile
def stl_vertices(path):
    # OpenSCAD writes ASCII STL; the copies in the repo are binary to stay small. Both are read here.
    data = open(path, "rb").read()
    if len(data) >= 84 and len(data) == 84 + 50 * struct.unpack_from("<I", data, 80)[0]:
        for i in range(struct.unpack_from("<I", data, 80)[0]):
            yield from (struct.unpack_from("<3f", data, 84 + 50 * i + 12 * k) for k in (1, 2, 3))
    else:
        for line in data.decode().splitlines():
            t = line.split()
            if t and t[0] == "vertex":
                yield tuple(float(x) for x in t[1:4])
def read_stl(path):
    verts, index, tris, cur = [], {}, [], []
    for p in stl_vertices(path):
        v = tuple(round(x, 5) for x in p)
        if v not in index:
            index[v] = len(verts); verts.append(v)
        cur.append(index[v])
        if len(cur) == 3:
            if len(set(cur)) == 3: tris.append(tuple(cur))
            cur = []
    return verts, tris
def mesh(oid, name, path):
    v, t = read_stl(path)
    vs = "".join(f'<vertex x="{a}" y="{b}" z="{c}"/>' for a, b, c in v)
    ts = "".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in t)
    return f'<object id="{oid}" name="{name}" type="model"><mesh><vertices>{vs}</vertices><triangles>{ts}</triangles></mesh></object>'
HEAD = ('<?xml version="1.0" encoding="UTF-8"?>'
        '<model unit="millimeter" xml:lang="de-DE" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">')
if sys.argv[1] == "--knoepfe":
    # The ten knobs of knopf.scad, body and pointer as one object each, in two rows of five.
    body, marks, out = sys.argv[2:5]
    objects = "".join(f'<object id="{3 + i}" name="Knopf {i + 1}" type="model"><components>'
                      f'<component objectid="1"/><component objectid="2"/></components></object>' for i in range(10))
    items = "".join(f'<item objectid="{3 + i}" transform="1 0 0 0 1 0 0 0 1 {100 + (i % 5 - 2) * 18} {100 + (i // 5 - 0.5) * 18} 0"/>'
                    for i in range(10))
    model = (HEAD + '<resources>' + mesh(1, "Körper", body) + mesh(2, "Zeiger", marks) + objects +
             '</resources><build>' + items + '</build></model>')
else:
    plate, text, out = sys.argv[1:4]
    model = (HEAD + '<resources>' + mesh(1, "Platte", plate) + mesh(2, "Schrift", text) +
             '<object id="3" name="Tonwerk Wavetable Frontplatte" type="model"><components>'
             '<component objectid="1"/><component objectid="2"/></components></object>'
             '</resources><build><item objectid="3" transform="1 0 0 0 1 0 0 0 1 70 70 0"/></build></model>')
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("[Content_Types].xml", '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    z.writestr("_rels/.rels", '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    z.writestr("3D/3dmodel.model", model)
