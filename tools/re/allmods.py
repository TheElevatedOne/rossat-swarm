import re,json,sys,urllib.request,urllib.parse
xml=open(sys.argv[1],encoding='utf-8-sig').read()
prods=re.findall(r'<PRODUCT ([^>]*)/>',xml)
pids={}
for p in prods:
    a=dict(re.findall(r'(\w+)="([^"]*)"',p))
    if 'pid' in a and int(a['pid'],16)!=0 and 'dll_name' in a:
        pids.setdefault('%04X'%int(a['pid'],16),a)
hw=','.join('"%s":{"driver":0,"firmware":0}'%k for k in pids)
sw=','.join('"%s":0'%k for k in ['2712','2713','2714','2715','2716','2711'])
data='data='+'{"system":10,"version":19482,"protocol":2,"hardware":{%s},"software":{%s}}'%(hw,sw)
req=urllib.request.Request('https://acpv.prod.turtlebeach.com/swarm1/autoupdate/SWARM',data=data.encode(),headers={'Content-Type':'application/x-www-form-urlencoded'})
r=json.load(urllib.request.urlopen(req,timeout=60))
json.dump({'settings':pids,'reply':r},open(sys.argv[2],'w'),indent=1)
h=r[1]['hardware'] or {}
print(len(pids),'pids queried;',len(h),'hardware modules;', len(r[1]['software'] or {}),'software')
