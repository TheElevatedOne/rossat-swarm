# usage: qrc.py PE outdir tree names data   (VAs hex)
import pefile,struct,sys,os,zlib
pe=pefile.PE(sys.argv[1]); base=pe.OPTIONAL_HEADER.ImageBase
img=pe.get_memory_mapped_image()
out=sys.argv[2]; tree,names,data=[int(x,16)-base for x in sys.argv[3:6]]
def be32(o): return struct.unpack_from('>I',img,o)[0]
def be16(o): return struct.unpack_from('>H',img,o)[0]
def name(off):
    o=names+off; ln=be16(o); return img[o+6:o+6+ln*2].decode('utf-16-be')
NODE=14
def walk(i,path):
    o=tree+i*NODE
    nm=name(be32(o)) if i else ''
    flags=be16(o+4)
    p=os.path.join(path,nm)
    if flags&2:
        cnt=be32(o+6); first=be32(o+10)
        for c in range(first,first+cnt): walk(c,p)
    else:
        doff=be32(o+10); ln=be32(data+doff); d=bytes(img[data+doff+4:data+doff+4+ln])
        if flags&1: d=zlib.decompress(d[4:])
        os.makedirs(os.path.dirname(p),exist_ok=True); open(p,'wb').write(d)
walk(0,out)
