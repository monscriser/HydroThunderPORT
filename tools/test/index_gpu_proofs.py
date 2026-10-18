"""Index final nonzero counters and all validation errors, preserving scope/hash."""
import json,re,sys
from pathlib import Path
root=Path(sys.argv[1]); rows=[]
tags=['ht-fconst-verify','ht-direct-verify','ht-dres-verify','ht-rrfast-verify','ht-rtt','ht-zpd-pair-verify','ht-dynamic-constants']
for p in root.rglob('game.log'):
    lines=p.read_text(encoding='utf8',errors='replace').splitlines()
    row={'log':str(p.relative_to(root)),'last_counters':{},'validation_errors':[]}
    for line in lines:
        if 'Vulkan Error' in line or 'Validation Error' in line:row['validation_errors'].append(line)
        for tag in tags:
            if '['+tag+']' in line:
                fields=dict((k,int(v)) for k,v in re.findall(r'([A-Za-z_]+)=(\d+)\b',line))
                if fields:row['last_counters'][tag]=fields
    row['positive_control']='positive' in str(p)
    rows.append(row)
(root/'proof-index.json').write_text(json.dumps(rows,indent=2,ensure_ascii=False),encoding='utf8')
for r in rows:
    if any(t in r['last_counters'] for t in ['ht-zpd-pair-verify','ht-fconst-verify']):
        print(json.dumps(r,ensure_ascii=False))
