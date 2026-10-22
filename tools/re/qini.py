import sys,re
txt=open(sys.argv[1],'rb').read().decode('latin1')
def unesc(s):
    out=bytearray(); i=0
    while i<len(s):
        c=s[i]
        if c=='\\':
            n=s[i+1]
            if n=='x':
                m=re.match(r'[0-9a-fA-F]{1,2}',s[i+2:]); out.append(int(m.group(),16)); i+=2+len(m.group()); continue
            if n in '01234567':
                m=re.match(r'[0-7]{1,3}',s[i+1:]); out.append(int(m.group(),8)); i+=1+len(m.group()); continue
            out.append({'a':7,'b':8,'f':12,'n':10,'r':13,'t':9,'v':11,'\\':92,'"':34}.get(n,ord(n))); i+=2; continue
        out.append(ord(c)); i+=1
    return bytes(out)
for line in txt.split('\r\n'):
    m=re.match(r'(\w+)="?@ByteArray\((.*)\)"?$',line)
    if m:
        b=unesc(m.group(2)); print(m.group(1),len(b)); print('  ',b.hex(' '))
    elif line: print(line[:200])
