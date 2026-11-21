import sys,subprocess,os
from PIL import Image
pdf,page,ours,out=sys.argv[1],int(sys.argv[2]),sys.argv[3],sys.argv[4]
subprocess.run(['pdftoppm','-r','72','-f',str(page+1),'-l',str(page+1),'-png','-singlefile',pdf,'/tmp/ref'],check=True)
a=Image.open('/tmp/ref.png').convert('RGB'); b=Image.open(ours).convert('RGB')
im=Image.new('RGB',(a.width+b.width+10,max(a.height,b.height)),(200,0,200))
im.paste(a,(0,0)); im.paste(b,(a.width+10,0)); im.save(out)
