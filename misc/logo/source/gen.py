import re, io, os, cairosvg
from PIL import Image, ImageFilter
SP='misc/logo/source/'
R=''
D=open(SP+'path.txt').read()
# Letter bbox in source coords
BX,BY,BW,BH=75.0,146.5,354.5,209.5
C1,C2='#4361ff','#8b3dff'

def letters(cx,cy,w,fill,extra=''):
    s=w/BW
    tx=cx-(BX+BW/2)*s; ty=cy-(BY+BH/2)*s
    return f'<path fill="{fill}" fill-rule="evenodd" {extra} transform="matrix({s:.5f} 0 0 {s:.5f} {tx:.3f} {ty:.3f})" d="{D}"/>'

def tile_svg(size, pad_ratio=0.0, shadow=True, rx_ratio=0.22, stroke=False):
    S=size; p=S*pad_ratio; w=S-2*p; rx=w*rx_ratio
    out=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{S}" height="{S}" viewBox="0 0 {S} {S}">',
         f'<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{C1}"/><stop offset="1" stop-color="{C2}"/></linearGradient>'
         f'<linearGradient id="h" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".22"/><stop offset=".5" stop-color="#fff" stop-opacity="0"/></linearGradient></defs>',
         f'<rect x="{p:.2f}" y="{p:.2f}" width="{w:.2f}" height="{w:.2f}" rx="{rx:.2f}" fill="url(#g)"'+(f' stroke="#1b1550" stroke-width="{S/64:.2f}"' if stroke else '')+'/>',
         f'<rect x="{p:.2f}" y="{p:.2f}" width="{w:.2f}" height="{w:.2f}" rx="{rx:.2f}" fill="url(#h)"/>']
    lw=w*0.74; cx=S/2; cy=S/2+w*0.01
    if shadow:
        o=w*0.018
        out.append(letters(cx+o,cy+o*1.6,lw,'#0b0730',' fill-opacity=".45"'))
    out.append(letters(cx,cy,lw,'#fff'))
    out.append('</svg>')
    return ''.join(out)

def wordmark_svg(h, fill=None, gradient=True, outline=False):
    s=h/BH; w=BW*s; m=h*0.06
    W=w+2*m; H=h+2*m
    out=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.0f}" height="{H:.0f}" viewBox="0 0 {W:.2f} {H:.2f}">']
    f=fill
    if gradient:
        out.append(f'<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="{C1}"/><stop offset="1" stop-color="{C2}"/></linearGradient></defs>')
        f='url(#g)'
    extra=f' stroke="#fff" stroke-width="{6/s:.2f}" stroke-linejoin="round" paint-order="stroke"' if outline else ''
    out.append(letters(W/2,H/2,w,f,extra))
    out.append('</svg>')
    return ''.join(out)

def png(svg, w, h=None):
    return Image.open(io.BytesIO(cairosvg.svg2png(bytestring=svg.encode(),output_width=w,output_height=h or w))).convert('RGBA')

def tile_png(size, pad_ratio=0.0):
    # Rasterized tile with a soft blurred letter shadow (nicer than SVG offset shadow).
    base=png(tile_svg(1024,pad_ratio,shadow=False).replace('<path fill="#fff"','<path fill="#fff" opacity="0"'),1024)
    mask=png(tile_svg(1024,pad_ratio,shadow=False).replace('url(#g)','none').replace('url(#h)','none'),1024)
    sh=Image.new('RGBA',(1024,1024),(10,6,45,0)); a=mask.split()[3].point(lambda v:int(v*.6))
    sh.putalpha(a); sh=sh.transform(sh.size,Image.AFFINE,(1,0,-14,0,1,-22)).filter(ImageFilter.GaussianBlur(18))
    clip=base.split()[3]
    shc=Image.new('RGBA',(1024,1024),(0,0,0,0)); shc.paste(sh,(0,0),None)
    from PIL import ImageChops
    sa=ImageChops.multiply(shc.split()[3],clip); shc.putalpha(sa)
    img=Image.alpha_composite(base,shc); img=Image.alpha_composite(img,mask)
    return img.resize((size,size),Image.LANCZOS)

def wr(path,data):
    p=R+path; os.makedirs(os.path.dirname(p),exist_ok=True)
    open(p,'w').write(data)

# --- master sources ---
wr('misc/logo/icon.svg',tile_svg(1024,0.0))
wr('misc/logo/icon_outlined.svg',tile_svg(1024,0.02,stroke=True))
wr('misc/logo/logo.svg',wordmark_svg(400))
wr('misc/logo/logo_outlined.svg',wordmark_svg(400,outline=True))
tile_png(256).save(R+'misc/logo/icon.png',optimize=True)
tile_png(256,0.02).save(R+'misc/logo/icon_outlined.png',optimize=True)
png(wordmark_svg(400),512,int(512*(400*1.12)/(BW*400/BH+48))).save(R+'misc/logo/logo.png',optimize=True)
png(wordmark_svg(400,outline=True),512,int(512*(400*1.12)/(BW*400/BH+48))).save(R+'misc/logo/logo_outlined.png',optimize=True)

# --- editor icons ---
wr('editor/icons/Godot.svg',tile_svg(16,0.03,shadow=False))
wr('editor/icons/DefaultProjectIcon.svg',tile_svg(128,0.015,stroke=True))
wr('editor/icons/Logo.svg',wordmark_svg(64))
mono=wordmark_svg(14,fill='#e0e0e0',gradient=False)
# monochrome 16x16 glyph: letters centered in 16px
wr('editor/icons/GodotMonochrome.svg','<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16">'+letters(8,8,15.5,'#e0e0e0')+'</svg>')
s=22/BH; wtb=BW*s
wr('editor/icons/TitleBarLogo.svg',f'<svg xmlns="http://www.w3.org/2000/svg" width="{wtb+2:.0f}" height="24">'+letters((wtb+2)/2,12,wtb,'#e0e0e0')+'</svg>')
gf=open(R+'editor/icons/GodotFile.svg').read()
outline=re.search(r'<path fill="#fff" fill-opacity="\.6"[^>]*/>',gf)
if outline:
    t=tile_svg(30,0,shadow=False)
    inner=re.sub(r'^<svg[^>]*>|</svg>$','',t).replace('id="g"','id="gf"').replace('url(#g)','url(#gf)').replace('id="h"','id="hf"').replace('url(#h)','url(#hf)')
    wr('editor/icons/GodotFile.svg','<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64"><g transform="translate(17 19)">'+inner+'</g>'+outline.group(0)+'</svg>')

# --- boot splash & app icon ---
tile_png(128).save(R+'main/app_icon.png',optimize=True)
def splash(path):
    im=Image.new('RGBA',(800,600),(0,0,0,0)); t=tile_png(300); im.paste(t,(250,150),t); im.save(R+path,optimize=True)
for p in ['main/splash.png','misc/dist/apple_embedded_xcode/godot_apple_embedded/Images.xcassets/SplashImage.imageset/splash@2x.png','misc/dist/apple_embedded_xcode/godot_apple_embedded/Images.xcassets/SplashImage.imageset/splash@3x.png','platform/android/java/editor/src/horizonos/assets/vr_splash.png','platform/android/java/editor/src/picoos/assets/vr_splash.png']:
    splash(p)

# --- web ---
wr('misc/dist/html/logo.svg',tile_svg(1024,0.0))

# --- windows ---
big=tile_png(256)
sizes=[(16,16),(32,32),(48,48),(64,64),(128,128),(256,256)]
big.save(R+'platform/windows/godot.ico',sizes=sizes)
big.save(R+'platform/windows/godot_console.ico',sizes=sizes)
ic=open(R+'misc/dist/windows/icon_console.svg').read()

# --- macOS ---
mac=tile_png(1024,0.09)  # macOS icons keep a margin
mac.save(R+'misc/dist/macos_tools.app/Contents/Resources/GodotLG.icns')
mac.save(R+'misc/dist/macos_template.app/Contents/Resources/icon.icns')

# --- android ---
res=R+'platform/android/java/lib/src/main/res/'
dens={'mipmap':(192,108),'mipmap-mdpi':(48,108),'mipmap-hdpi':(72,162),'mipmap-xhdpi':(96,216),'mipmap-xxhdpi':(144,324),'mipmap-xxxhdpi':(192,432)}
for d,(ic_s,ad_s) in dens.items():
    tile_png(ic_s,0.04).save(res+d+'/icon.webp',lossless=True)
    bg=png(tile_svg(1024,0,shadow=False,rx_ratio=0).replace('<path fill="#fff"','<path fill="#fff" opacity="0"'),ad_s).convert('RGB')
    bg.save(res+d+'/icon_background.webp',lossless=True)
    # adaptive icon foreground: letters within the 66% safe zone
    fg='<svg xmlns="http://www.w3.org/2000/svg" width="108" height="108">'+letters(54.7,55.6,50,'#0b0730',' fill-opacity=".4"')+letters(54,54.5,50,'#fff')+'</svg>'
    png(fg,ad_s).save(res+d+'/icon_foreground.webp',lossless=True)
    mo='<svg xmlns="http://www.w3.org/2000/svg" width="108" height="108">'+letters(54,54.5,50,'#000')+'</svg>'
    png(mo,ad_s).save(res+d+'/icon_monochrome.webp',lossless=True)
tile_png(432,0.1).save(res+'drawable/splash_icon.webp',lossless=True)
print('done')

# --- windows console icon source (tile + terminal badge) ---
t=tile_svg(1024,0.0)
badge='<rect x="600" y="660" width="380" height="300" rx="48" fill="#0d1024" stroke="#fff" stroke-width="20"/><path d="M660 740l90 70-90 70" fill="none" stroke="#fff" stroke-width="36" stroke-linecap="round" stroke-linejoin="round"/><path d="M790 890h130" stroke="#fff" stroke-width="36" stroke-linecap="round"/>'
wr('misc/dist/windows/icon_console.svg',t.replace('</svg>',badge+'</svg>'))
con=png(t.replace('</svg>',badge+'</svg>'),256)
con.save(R+'platform/windows/godot_console.ico',sizes=sizes)

# --- macOS Icon Composer (.icon) ---
import json
icd=R+'misc/dist/macos/GodotLG.icon/'
j=json.load(open(icd+'icon.json'))
j['fill-specializations']=[{"value":{"automatic-gradient":"srgb:0.31373,0.33333,1.00000,1.00000"}},{"appearance":"dark","value":"automatic"}]
g=j['groups'][0]
g['layers']=[{"fill":{"automatic-gradient":"extended-gray:1.00000,1.00000"},"image-name":"layer_0.svg","name":"layer_0"}]
json.dump(j,open(icd+'icon.json','w'),indent=2)
for f in ['layer_1.svg','layer_2.svg']:
    if os.path.exists(icd+'Assets/'+f): os.remove(icd+'Assets/'+f)
wr('misc/dist/macos/GodotLG.icon/Assets/layer_0.svg','<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024">'+letters(512,520,760,'#fff')+'</svg>')
print('extra done')
