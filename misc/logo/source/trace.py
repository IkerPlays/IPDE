import numpy as np, potrace
from PIL import Image
im=Image.open('misc/logo/source/ipde_original.png').convert('RGBA')
# upscale 4x for smoother tracing
S=4
im=im.resize((im.width*S,im.height*S),Image.LANCZOS)
a=np.array(im).astype(int)
mask=(a[:,:,0]>160)&(a[:,:,3]>160)
ys,xs=np.where(mask); print('bbox',xs.min()/S,ys.min()/S,xs.max()/S,ys.max()/S)
bm=potrace.Bitmap(~mask)
path=bm.trace(turdsize=50,alphamax=0.8,opticurve=True,opttolerance=0.2)
d=[]
f=lambda p:f"{p.x/S:.2f} {p.y/S:.2f}"
for c in path:
    d.append("M"+f(c.start_point))
    for s in c.segments:
        if s.is_corner: d.append("L"+f(s.c)+" L"+f(s.end_point))
        else: d.append("C"+f(s.c1)+" "+f(s.c2)+" "+f(s.end_point))
    d.append("Z")
open('misc/logo/source/path.txt','w').write("".join(d))
print(len(path), len("".join(d)))
