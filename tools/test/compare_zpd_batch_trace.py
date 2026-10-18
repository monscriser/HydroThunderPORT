"""Exact comparison ONLY for report addresses stable in the reference tail.
Dynamic addresses are explicitly excluded, not silently counted as passing.
"""
import sys,re,json,collections
from pathlib import Path
def read(path):
    values=collections.defaultdict(list)
    for line in Path(path).read_text(encoding='utf8',errors='replace').splitlines():
        m=re.search(r'\[ht-zpd-batch-result\] key=([A-F0-9]+) raw=(\d+) samples=(\d+) parts=(\d+)',line)
        if m and m[1]!='FFFFFFFF': values[m[1]].append(tuple(map(int,m.groups()[1:])))
    return {key:val[-120:] for key,val in values.items() if len(val)>=120}
a,b=read(sys.argv[1]),read(sys.argv[2])
stable={key:v[-1][:2] for key,v in a.items() if len(set(x[:2] for x in v))==1}
rows=[];comparisons=mismatches=nonzero=positive=0
for key,expected in stable.items():
    if key not in b: continue
    bad=sum(x[:2]!=expected for x in b[key])
    mismatches+=bad;comparisons+=len(b[key]);nonzero+=sum(x[0]>0 for x in b[key])
    positive+=((expected[0]+1,expected[1])!=expected)
    rows.append({'key':key,'reference_raw':expected[0],'compared':len(b[key]),'mismatches':bad,'parts_max':max(x[2] for x in b[key])})
result={'scope':'Only addresses constant across last 120 reference intervals/address; no full-frame equivalence claim',
        'reference_addresses':len(a),'candidate_addresses':len(b),'stable_reference_addresses':len(stable),
        'compared':comparisons,'mismatches':mismatches,'nonzero_candidate_samples':nonzero,
        'positive_detected_addresses':positive,'excluded_dynamic_addresses':sorted(set(a)-set(stable)),
        'rows':rows}
Path(sys.argv[3]).write_text(json.dumps(result,indent=2),encoding='utf8')
print(json.dumps({k:v for k,v in result.items() if k!='rows'},indent=2))
