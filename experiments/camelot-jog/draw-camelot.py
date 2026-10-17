from pathlib import Path
import math, colorsys
from PIL import Image, ImageDraw, ImageFont
out=Path(__file__).resolve().parent/'artwork'
out.mkdir(exist_ok=True)
font='/System/Library/Fonts/Supplemental/Arial Bold.ttf'
S=1024
for num,letter,name in [(7,'A','D minor'),(2,'A','E-flat minor')]:
    im=Image.new('RGB',(S,S),(9,12,18)); d=ImageDraw.Draw(im)
    c=S/2
    for ring,(ri,ro,letter2) in enumerate([(267,373,'A'),(382,490,'B')]):
        for n in range(1,13):
            a0=-105+(n-1)*30+0.8; a1=a0+28.4
            same=n==num and letter2==letter
            compat=(letter2==letter and n in [((num-2)%12)+1,num%12+1]) or (n==num and letter2!=letter)
            hue=(n-1)/12
            rgb=tuple(int(v*255) for v in colorsys.hsv_to_rgb(hue,.6,.85 if same else .6 if compat else .22))
            pts=[]
            for a in range(41):
                t=math.radians(a0+(a1-a0)*a/40); pts.append((c+ro*math.cos(t),c+ro*math.sin(t)))
            for a in range(40,-1,-1):
                t=math.radians(a0+(a1-a0)*a/40); pts.append((c+ri*math.cos(t),c+ri*math.sin(t)))
            d.polygon(pts,fill=rgb)
            if same: d.line(pts+[pts[0]],fill='white',width=6)
            a=math.radians((a0+a1)/2); r=(ri+ro)/2
            d.text((c+r*math.cos(a),c+r*math.sin(a)),f'{n}{letter2}',font=ImageFont.truetype(font,46),anchor='mm',fill='white' if same or compat else '#87909d')
    d.text((512,403),'TRACK KEY',font=ImageFont.truetype(font,32),anchor='mm',fill='#8e9cae')
    d.text((512,504),f'{num}{letter}',font=ImageFont.truetype(font,160),anchor='mm',fill='white')
    d.text((512,615),name,font=ImageFont.truetype(font,39),anchor='mm',fill='#d7deeb')
    im.resize((512,512),Image.Resampling.LANCZOS).save(out/f'{num}{letter}.jpg',quality=93,subsampling=0)
    im.resize((160,160),Image.Resampling.LANCZOS).save(out/f'{num}{letter}_m.jpg',quality=93)
print(out)
