import sys,struct,pefile
pe=pefile.PE(sys.argv[1]); base=pe.OPTIONAL_HEADER.ImageBase
s=[x for x in pe.sections if x.Name.startswith(b'.text')][0]
t=s.get_data(); va=base+s.VirtualAddress
targets={int(x,16) for x in sys.argv[2:]}
for i in range(len(t)-5):
    if t[i] in (0xe8,0xe9):
        dst=va+i+5+struct.unpack_from('<i',t,i+1)[0]
        if dst in targets: print(hex(va+i),hex(dst))
