# Render a .dl display list to PNG (approximation of the GEOS app's replay).
import sys, struct
from PIL import Image, ImageDraw, ImageFont
PAL=[(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),(170,0,170),(170,85,0),(170,170,170),
     (85,85,85),(85,85,255),(85,255,85),(85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]
LIB='/usr/share/fonts/truetype/liberation/Liberation%s-%s.ttf'
FAM={0x1000:'Serif',0x1200:'Sans',0x1a00:'Mono'}
def font(fid,size,style):
    st={0:'Regular',0x20:'Bold',0x10:'Italic',0x30:'BoldItalic'}[style&0x30]
    return ImageFont.truetype(LIB%(FAM.get(fid,'Sans'),st),max(1,int(round(size))))
import os
data=open(sys.argv[1],'rb').read()
ctf=sys.argv[1][:-3]+'.ct'
if os.path.exists(ctf):
    ct=open(ctf,'rb').read()
    PAL=PAL+[tuple(ct[4*k:4*k+3]) for k in range(240)]
w,h=struct.unpack('<hh',data[:4]); dl=data[4:]
img=Image.new('RGB',(max(1,w),max(1,h)),'white'); dr=ImageDraw.Draw(img)
i=0; nrect=ntext=0
EXT={}
for k in range(4):
    fn=sys.argv[1][:-3]+'.e%d'%k
    if os.path.exists(fn): EXT[k]=open(fn,'rb').read()
stack=[]
while True:
    if i>=len(dl):
        if stack: dl,i=stack.pop(); continue
        break
    op=dl[i]
    if op==0:
        if stack: dl,i=stack.pop(); continue
        break
    if op==6:
        blk=dl[i+1]; off,ln=struct.unpack('<HH',dl[i+2:i+6])
        stack.append((dl,i+6)); dl=EXT[blk]; i=off; continue
    if op==1:
        c=dl[i+1]; x0,y0,x1,y1=struct.unpack('<hhhh',dl[i+2:i+10]); i+=10
        dr.rectangle([x0,y0,x1,y1],fill=PAL[c]); nrect+=1
    elif op==2:
        c=dl[i+1]; fid,size=struct.unpack('<HH',dl[i+2:i+6]); st=dl[i+6]
        x,y=struct.unpack('<hh',dl[i+7:i+11]); n=dl[i+11]; s=dl[i+12:i+12+n]; i+=12+n
        txt=s.decode('mac_roman')
        dr.text((x,y),txt,fill=PAL[c],font=font(fid,size/4.0,st)); ntext+=1
    elif op==4:
        c=dl[i+1]; x,y,ln=struct.unpack('<hhH',dl[i+2:i+8]); bw,bh,comp,typ=struct.unpack('<HHBB',dl[i+8:i+14])
        data=dl[i+14:i+8+ln]; i+=8+ln; rb=(bw+7)//8; rows=bytearray(); j=0
        if comp==1:
            while j<len(data):
                n=data[j]; j+=1
                if n<128: rows+=data[j:j+n+1]; j+=n+1
                elif n>128: rows+=bytes([data[j]])*(257-n); j+=1
        else: rows=bytearray(data)
        if typ&0x0f==1:
            mb=rb if typ&0x10 else 0; pb=(bw+1)//2; rl=mb+pb
            assert len(rows)==rl*bh,(len(rows),rl*bh)
            for yy in range(bh):
                r=rows[yy*rl:(yy+1)*rl]
                for xx in range(bw):
                    if mb and not r[xx>>3]&(0x80>>(xx&7)): continue
                    v=r[mb+(xx>>1)]; v=(v>>4) if xx%2==0 else v&15
                    s=c if c>=2 else 1
                    for dy in range(s):
                        for dx in range(s):
                            X=x+xx*s+dx; Y=y+yy*s+dy
                            if 0<=X<img.width and 0<=Y<img.height: img.putpixel((X,Y),PAL[v])
            nrect+=1; continue
        assert len(rows)==rb*bh,(len(rows),rb*bh)
        for yy in range(bh):
            for xx in range(bw):
                if rows[yy*rb+(xx>>3)]&(0x80>>(xx&7)): img.putpixel((x+xx,y+yy),PAL[c]) if 0<=x+xx<img.width and 0<=y+yy<img.height else None
        nrect+=1
    elif op==5:
        s=dl[i+1]; x,y,ln,rows=struct.unpack('<hhHH',dl[i+2:i+10]); j=i+10; i+=8+ln
        for r in range(rows):
            nr=struct.unpack('<H',dl[j:j+2])[0]; j+=2
            for _ in range(nr):
                dx,bw=struct.unpack('<HH',dl[j:j+4]); j+=8
                for k in range(bw):
                    c=tuple(dl[j+3*k:j+3*k+3])
                    for yy in range(s):
                        for xx in range(s):
                            X=x+(dx+k)*s+xx; Y=y+r*s+yy
                            if 0<=X<img.width and 0<=Y<img.height: img.putpixel((X,Y),c)
                j+=3*bw
        assert j==i,(j,i)
        nrect+=1
    else:
        print('bad op',op,'at',i); break
img.save(sys.argv[2]); print(sys.argv[2],w,h,'rects',nrect,'texts',ntext)
