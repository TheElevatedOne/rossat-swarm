import sys,re
src=open(sys.argv[1]).read()
parts=re.split(r'(?m)^// ==== ',src)
idx={}
for p in parts[1:]:
    head=p.split('\n',1)[0]
    name,_,addr=head.partition(' @ ')
    idx[name]=p; idx[addr.strip()]=p
for a in sys.argv[2:]:
    for k in (a,'FUN_'+a):
        if k in idx: print('// ==== '+idx[k]); break
    else: print('NOTFOUND',a)
