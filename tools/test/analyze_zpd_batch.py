"""Use the recorded race window; absent data stays null."""
import sys, json, re, statistics
from datetime import datetime,timedelta
from pathlib import Path
root=Path(sys.argv[1])
report=[]
for d in sorted(root.iterdir()):
    if not (d/'game.log').exists() or not (d/'nvidia-smi.csv').exists(): continue
    rows=(d/'nvidia-smi.csv').read_text(encoding='utf-8-sig').splitlines()
    if not rows: continue
    stamps=[datetime.strptime(r.split(',')[0].strip(),'%Y/%m/%d %H:%M:%S.%f') for r in rows]
    start=stamps[0]+timedelta(seconds=15)
    end=stamps[-1]
    lines=[]
    for line in (d/'game.log').read_text(encoding='utf-8-sig',errors='replace').splitlines():
        match=re.match(r'\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]',line)
        if match and start<=datetime.strptime(match.group(1),'%Y-%m-%d %H:%M:%S.%f')<=end:
            lines.append(line)
    def avg(tag, key):
        vals=[]
        for l in lines:
            if tag not in l: continue
            match=re.search(r'\b'+re.escape(key)+r'=([\d.]+)',l)
            if match: vals.append(float(match.group(1)))
        return round(statistics.mean(vals),4) if vals else None
    row={'run':d.name,'window_start':str(start),'window_end':str(end),'window_basis':'first GPU CSV timestamp +15s through last GPU CSV timestamp; excludes shutdown',
         'gpu_ms':avg('[ht-perf]','total'), 'passes':avg('[ht-perf]','rp'),
         'draws':avg('[ht-perf]','draws'),'submissions':avg('[ht-perf]','subm'),
         'cpu_command_ms':avg('[ht-cpu]','busy'), 'fps':avg('[ht-present]','fps')}
    row['cpu_draw_ms']=avg('[ht-draw]','total')
    row['cpu_bindings_ms']=avg('[ht-draw]','bindings')
    row['cpu_primitive_ms']=avg('[ht-draw]','prim')
    row['worst_logged_frame_ms']=max([float(m.group(1)) for l in lines if '[ht-present]' in l and (m:=re.search(r'worst=([\d.]+)',l))],default=None)
    row['present_samples']=sum('[ht-present]' in l for l in lines)
    row['performance_run']=not any(word in d.name for word in ['proof','positive','standing','trace','detail'])
    row['cpu_command_us_per_draw']=round(row['cpu_command_ms']*1000/row['draws'],4) if row['draws'] else None
    stats=[l for l in lines if '[ht-zpd-batch-stats]' in l]
    row['zpd_stats_last']=stats[-1] if stats else None
    report.append(row)
(root/'summary.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf8')
print(json.dumps(report,indent=2,ensure_ascii=False))
