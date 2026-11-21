# Generates the PDF-Betrachter icon bitmaps (own design: page with folded
# corner and a "PDF" band) in the formats GeoWorks 1 uses for app icons.
from PIL import Image
PAL=[(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),(170,85,0),(170,170,170),
     (85,85,85),(85,85,255),(85,255,85),(85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]
T=-1  # transparent
F5={'P':["111","101","111","100","100"],'D':["110","101","101","101","110"],'F':["111","100","110","100","100"]}
F3={'P':["111","111","100"],'D':["110","101","110"],'F':["111","110","100"]}
def doc(w,h,x0,y0,dw,dh,fold,line,fill,band,bandcol,txtcol,font,sx=1,sy=1,lw=1):
    img=[[T]*w for _ in range(h)]
    x1,y1=x0+dw-1,y0+dh-1
    def inside(x,y):  # page shape with cut corner
        if x<x0 or x>x1 or y<y0 or y>y1: return False
        return not (x>x1-fold and y<y0+(fold*(x-(x1-fold)))//fold+ (0))  or (x-(x1-fold))<= (y-y0)
    for y in range(y0,y1+1):
        for x in range(x0,x1+1):
            if (x-(x1-fold)) > (y-y0) * (1 if True else 1) * fold // fold and x>x1-fold and y<y0+fold: continue
            img[y][x]=fill
    # outline
    for y in range(y0,y1+1):
        for x in range(x0,x1+1):
            if img[y][x]==T: continue
            edge=False
            for dx in range(-lw,lw+1):
                for dy in range(-lw,lw+1):
                    xx,yy=x+dx,y+dy
                    if xx<0 or yy<0 or xx>=w or yy>=h or img[yy][xx]==T: edge=True
            if edge: img[y][x]=line
    # fold triangle
    for y in range(y0,y0+fold):
        for x in range(x1-fold+1,x1+1):
            if x-(x1-fold) <= y-y0: img[y][x]=line
    # band with letters
    by0,by1=band
    for y in range(by0,by1+1):
        for x in range(x0,x1+1):
            if img[y][x]!=T: img[y][x]=bandcol
    word="PDF"; gw=len(font['P'][0]); gh=len(font['P'])
    tw=(gw*3+2)*sx; tx=x0+(dw-tw)//2; ty=by0+((by1-by0+1)-gh*sy)//2
    for i,ch in enumerate(word):
        for r,row in enumerate(font[ch]):
            for c,bit in enumerate(row):
                if bit=='1':
                    for yy in range(sy):
                        for xx in range(sx):
                            img[ty+r*sy+yy][tx+(i*(gw+1)+c)*sx+xx]=txtcol
    return img
icons={
 # name: (w,h, color?, params)
 'c64x40':(64,40,True, dict(x0=17,y0=1,dw=30,dh=38,fold=9,line=4,fill=15,band=(21,31),bandcol=4,txtcol=15,font=F5,sx=2,sy=2,lw=1)),
 'm64x40':(64,40,False,dict(x0=17,y0=1,dw=30,dh=38,fold=9,line=0,fill=15,band=(21,31),bandcol=0,txtcol=15,font=F5,sx=2,sy=2,lw=1)),
 'c48x30':(48,30,True, dict(x0=13,y0=1,dw=22,dh=28,fold=7,line=4,fill=15,band=(15,23),bandcol=4,txtcol=15,font=F5,sx=1,sy=1,lw=1)),
 'm48x30':(48,30,False,dict(x0=13,y0=1,dw=22,dh=28,fold=7,line=0,fill=15,band=(15,23),bandcol=0,txtcol=15,font=F5,sx=1,sy=1,lw=1)),
 'm64x18':(64,18,False,dict(x0=22,y0=0,dw=20,dh=18,fold=6,line=0,fill=15,band=(9,13),bandcol=0,txtcol=15,font=F3,sx=1,sy=1,lw=1)),
 'm48x14':(48,14,False,dict(x0=16,y0=0,dw=16,dh=14,fold=5,line=0,fill=15,band=(7,11),bandcol=0,txtcol=15,font=F3,sx=1,sy=1,lw=1)),
}
out={}
for name,(w,h,color,p) in icons.items():
    out[name]=(w,h,color,doc(w,h,**p))
if __name__=='__main__':
    sheet=Image.new('RGB',(64*6+70,40*6),(0,128,128)); x=0
    for name,(w,h,color,img) in out.items():
        im=Image.new('RGB',(w,h),(0,170,170))
        for y in range(h):
            for xx in range(w):
                v=img[y][xx]
                if v!=T: im.putpixel((xx,y),PAL[v] if color else ((0,0,0) if v in (0,4) else (255,255,255)))
        sc=3 if w==64 else 3
        im=im.resize((w*sc,h*sc*(1 if h>20 else 2)),Image.NEAREST)
        sheet.paste(im,(x,0)); x+=im.width+6
    sheet=sheet.crop((0,0,x,max(40*3,36*3)))
    sheet.save('preview.png'); print('ok')
