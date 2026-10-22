import pefile,struct,sys,re
pe=pefile.PE(sys.argv[1]); base=pe.OPTIONAL_HEADER.ImageBase
img=pe.get_memory_mapped_image()
def u32(va): return struct.unpack_from('<I',img,va-base)[0]
def findall(b): return [m.start()+base for m in re.finditer(re.escape(b),img)]
text=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
tlo=base+text.VirtualAddress; thi=tlo+text.Misc_VirtualSize
for name in sys.argv[2:]:
    for sa in findall(name.encode()+b'\0'):
        for ti in findall(struct.pack('<I',sa)):
            ti-=4
            for vt in findall(struct.pack('<I',ti)):
                if u32(vt-4)!=0 and u32(vt-4) < 0x80000000 and u32(vt-4)!=0: pass
                fs=[]; a=vt+4
                while tlo<=u32(a)<thi: fs.append(hex(u32(a))); a+=4
                print(name,'typeinfo',hex(ti),'vtable@',hex(vt+4),'off',hex(u32(vt-4)),len(fs)); print('  ',' '.join(fs))
