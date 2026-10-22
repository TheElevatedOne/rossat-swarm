import json,sys
d=json.load(open(sys.argv[1])); s=d['settings']; h=d['reply'][1]['hardware'] or {}; sw=d['reply'][1]['software'] or {}
types={'0':'Mouse','1':'Keyboard','2':'Headset','3':'Mousepad / Dock'}
rows={}
for pid,a in s.items():
    if a.get('device_type') is None: continue
    m=h.get(pid)
    rows.setdefault(a['device_type'],[]).append((pid,a,m))
out=['# ROCCAT Swarm device modules','',
'Every device module that ROCCAT Swarm 1.9482 can download, as returned by the official',
'update server (`POST https://acpv.prod.turtlebeach.com/swarm1/autoupdate/SWARM`) when queried',
'with each USB product ID listed in the bundled `settings.xml`. Module versions are as of 2026-10-21.','',
'A checked box means rossat-swarm supports the device.','']
for t in sorted(rows,key=int):
    out+=['## '+types.get(t,t),'']
    for pid,a,m in sorted(rows[t],key=lambda r:(r[2]['name'] if r[2] else r[1].get('disp_name',''),r[0])):
        ok = pid=='2E2C'
        name = m['name'] if m else a.get('disp_name','').replace('_',' ').title()+' *(no module on server)*'
        out.append('- [%s] **%s** — `1E7D:%s` · module `%s`%s'%('x' if ok else ' ',name,pid,a['dll_name'],(' v%s (%.1f MB)'%(m['version'],m['size']/1e6)) if m else ''))
    out.append('')
out+=['## Software modules','']
for k,m in sw.items(): out.append('- [ ] **%s** — id `%s` · v%s'%(m['name'],k,m['version']))
out.append('')
open(sys.argv[2],'w').write('\n'.join(out))
