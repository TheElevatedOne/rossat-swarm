import struct,sys,json
d=open(sys.argv[1],'rb').read()
assert d[:16]==bytes.fromhex('3cb86418caef9c95cd211cbf60a1bddd')
i=16; msgs=None
while i<len(d):
    tag=d[i]; ln=struct.unpack_from('>I',d,i+1)[0]; blk=d[i+5:i+5+ln]; i+=5+ln
    if tag==0x69: msgs=blk
out=[]; j=0; cur={}
while j<len(msgs):
    t=msgs[j]; j+=1
    if t==1: out.append(cur); cur={}; continue
    if t in (3,):
        ln=struct.unpack_from('>I',msgs,j)[0]; j+=4
        if ln==0xffffffff: cur['tr']=None; continue
        cur['tr']=msgs[j:j+ln].decode('utf-16-be'); j+=ln
    elif t in (6,7,8):
        ln=struct.unpack_from('>I',msgs,j)[0]; j+=4
        cur[{6:'src',7:'ctx',8:'cmt'}[t]]=msgs[j:j+ln].decode('latin1'); j+=ln
    elif t==5: j+=4
    elif t==4: j+=4
    elif t==0: continue
    else: raise SystemExit('tag %d at %d'%(t,j))
json.dump(out,sys.stdout,ensure_ascii=False,indent=0)
