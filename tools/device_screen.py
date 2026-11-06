import sys,time
from pathlib import Path
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parent));from flipper_cli import CLI

def vi(v):
 b=bytearray()
 while v>127:b.append((v&127)|128);v>>=7
 b.append(v);return bytes(b)
def readvi(data,off=0):
 v=0;s=0
 while True:
  b=data[off];off+=1;v|=(b&127)<<s
  if b<128:return v,off
  s+=7

def fields(data):
 off=0
 while off<len(data):
  key,off=readvi(data,off);kind=key&7
  if kind==0:value,off=readvi(data,off)
  elif kind==2:
   n,off=readvi(data,off);value=data[off:off+n];off+=n
  else:raise ValueError(kind)
  yield key>>3,value
c=CLI();frame=None
try:
 c.s.write(b'start_rpc_session\r');c.until(b'\r\n')
 def send(cmd,field):
  payload=b'\x08'+vi(cmd)+vi((field<<3)|2)+b'\x00'
  c.s.write(vi(len(payload))+payload)
 def message():
  n=0;shift=0
  while True:
   b=c.exact(1)[0];n|=(b&127)<<shift
   if b<128:break
   shift+=7
  return dict(fields(c.exact(n)))
 send(1,20)
 for i in range(10):
  m=message()
  if 22 in m:
   frame=dict(fields(m[22]))[1];break
 send(2,21);send(3,19)
 # The pipe-based RPC CLI checks the close flag after its next read.
 # Wait for StopSession acknowledgement, then wake that read.
 for _ in range(20):
  if message().get(1)==3:break
 c.s.write(b'\r')
 c.until(b'>: ',10)
 assert frame is not None and len(frame)==1024
 im=Image.new('RGB',(128,64),(240,175,30))
 for y in range(64):
  for x in range(128):
   if frame[x+(y//8)*128]&(1<<(y%8)):im.putpixel((x,y),(25,20,10))
 im.resize((768,384),Image.Resampling.NEAREST).save(sys.argv[1] if len(sys.argv)>1 else 'screen.png')
 print('Screen saved')
finally:c.close()
