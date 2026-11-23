"""Actual-image regression against local sealed primary records; raw images and records are not bundled."""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import csv,ctypes as ct,hashlib,json,math,statistics,struct,subprocess,time

import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reference-runs',type=Path,required=True)
parser.add_argument('--bridge',type=Path,required=True)
parser.add_argument('--original-dll',type=Path,required=True)
parser.add_argument('--new-bin',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True)
args=parser.parse_args()
OLD=args.reference_runs.resolve()
BRIDGE=args.bridge.resolve()
ORIGINAL_DLL=args.original_dll.resolve()
RELEASE=args.new_bin.resolve()
OUT=args.out.resolve()
CONDITIONS=['clean','gaussian15','impulse2','gaussian30','brightness','blur1']

def dump(path,item):path.write_text(json.dumps(item,indent=2,allow_nan=False)+'\n',encoding='utf-8')
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def library(path):
    dll=ct.CDLL(str(path),winmode=0x1100)
    for function in [dll.KISA_Enroll,dll.KISA_Attempt]:
        function.argtypes=[ct.c_int,ct.c_void_p,ct.c_void_p,ct.POINTER(ct.c_int),ct.POINTER(ct.c_int),ct.c_void_p]
    dll.KISA_Compare.argtypes=[ct.c_void_p,ct.c_void_p,ct.POINTER(ct.c_double),ct.POINTER(ct.c_int),ct.c_void_p]
    return dll
def compare(dll,a,b):
    value=ct.c_double();error=ct.c_int();message=ct.create_string_buffer(256)
    start=time.perf_counter_ns()
    ret=dll.KISA_Compare(a,b,ct.byref(value),ct.byref(error),message)
    elapsed=(time.perf_counter_ns()-start)/1e6
    assert ret==1 and error.value==0,(error.value,message.value)
    return value.value,elapsed
def stats(values):
    values=sorted(values)
    return dict(n=len(values),mean_ms=statistics.mean(values),p95_ms=values[math.ceil(.95*len(values))-1],max_ms=max(values),over_250ms=sum(v>250 for v in values))

def payload_equal(new,old):
    fields=struct.unpack_from('<10I',new)
    _,version,total,_,width,height,dpi,cols,rows,count=fields
    assert version==2 and total==len(new)
    old_header=struct.unpack_from('<10I',old)
    assert old_header[0]==0x46504f31 and old_header[1]==1
    assert (cols,rows,width,height,dpi)==old_header[4:9]
    assert count==struct.unpack_from('<I',old,48)[0]
    psize=count*16;dsize=count*1536
    assert new[40:40+psize]==old[56:56+psize],'Point extraction changed'
    assert new[40+psize:40+psize+dsize]==old[2104:2104+dsize],'Descriptor changed'
    assert new[40+psize+dsize:]==old[198712:198712+cols*rows*8],'Direction field changed'

def run():
    if OUT.exists():raise FileExistsError('Preserve prior regression run')
    OUT.mkdir(parents=True)
    raw=library(RELEASE/'fingerprint_algorithm.dll')
    rounded=library(RELEASE/'fingerprint_algorithm_eval.dll')
    dump(OUT/'state.json',dict(status='RUNNING',raw_sha256=digest(RELEASE/'fingerprint_algorithm.dll'),eval_sha256=digest(RELEASE/'fingerprint_algorithm_eval.dll')))
    results=[];all_sizes=[];all_enroll=[];all_attempt=[];all_compare=[];samples=[];total_calls=0;max_delta=0
    for db in (1,2,3):
        for condition in CONDITIONS:
            old=OLD/f'orientation-db{db}-{condition}';dest=OUT/f'db{db}-{condition}';dest.mkdir();(dest/'templates').mkdir()
            lines=(old/'plan.tsv').read_text(encoding='utf-8').splitlines()
            plan=[line.split('\t') for line in lines[1:]]
            expected={row['pair_id']:row for row in csv.DictReader((old/'pairs.tsv').read_text().splitlines(),delimiter='\t')}
            assert len(plan)==790 and all(r['status']=='OK' for r in expected.values())
            provenance=read(old/'provenance.json')
            subprocess.run([str(BRIDGE),'images',str(old/'plan.tsv'),str(dest/'pixels'),condition,str(provenance['noise_seed']),'same-as-fp-bench'],check=True,capture_output=True)
            registry={row['path']:row for row in csv.DictReader((dest/'pixels/images.tsv').read_text(encoding='utf-8').splitlines(),delimiter='\t')}
            enroll=list(dict.fromkeys(r[4] for r in plan));attempt=list(dict.fromkeys(r[5] for r in plan))
            jobs=[(role,index,path) for role,paths in [('e',enroll),('a',attempt)] for index,path in enumerate(paths)]
            def extract(job):
                role,index,path=job;entry=registry[path]
                pixel=ct.create_string_buffer((dest/'pixels'/entry['enroll' if role=='e' else 'attempt']).read_bytes())
                output=ct.create_string_buffer(1000000);size=ct.c_int();error=ct.c_int();message=ct.create_string_buffer(256)
                start=time.perf_counter_ns()
                ret=(raw.KISA_Enroll if role=='e' else raw.KISA_Attempt)(db,pixel,output,ct.byref(size),ct.byref(error),message)
                elapsed=(time.perf_counter_ns()-start)/1e6
                assert ret==1 and error.value==0,(db,condition,path,error.value,message.value)
                value=output.raw[:size.value]
                payload_equal(value,(old/'templates'/f'{role}{index}.bin').read_bytes())
                (dest/'templates'/f'{role}{index}.bin').write_bytes(value)
                return role,index,ct.create_string_buffer(value),elapsed,len(value)
            with ThreadPoolExecutor(max_workers=4) as pool:extracted=list(pool.map(extract,jobs))
            templates={(role,index):value for role,index,value,_,_ in extracted}
            enroll_times=[ms for role,_,_,ms,_ in extracted if role=='e'];attempt_times=[ms for role,_,_,ms,_ in extracted if role=='a']
            sizes=[size for _,_,_,_,size in extracted]
            def match(row):
                a=templates['e',enroll.index(row[4])];b=templates['a',attempt.index(row[5])]
                score,elapsed=compare(raw,a,b)
                delta=abs(score-float(expected[row[0]]['score']))
                assert delta<=1e-12,(db,condition,row[0],score,expected[row[0]]['score'],delta)
                return dict(pair_id=row[0],kind=row[3],status='OK',stage='compare',score=score,call_ms=elapsed,delta=delta)
            with ThreadPoolExecutor(max_workers=4) as pool:records=list(pool.map(match,plan))
            for kind in ('G','I'):
                selected=[r for r in plan if r[3]==kind][:6]
                for row in selected:
                    a=templates['e',enroll.index(row[4])];b=templates['a',attempt.index(row[5])]
                    s,_=compare(rounded,a,b)
                    assert s==math.floor(float(expected[row[0]]['score'])*1000+.5)/1000
                samples.extend((old,dest,row,enroll.index(row[4]),attempt.index(row[5])) for row in selected)
            with (dest/'pairs.tsv').open('w',newline='',encoding='utf-8') as f:
                writer=csv.DictWriter(f,fieldnames=list(records[0]),delimiter='\t');writer.writeheader();writer.writerows(records)
            metric_input=dest/'metric-input.tsv'
            metric_input.write_text('genuine\tstatus\tstage\tscore\n'+''.join(f"{r['kind']}\tOK\tcompare\t{r['score']:.17g}\n" for r in records))
            subprocess.run([str(BRIDGE),'metrics',str(metric_input),str(dest/'metrics.json')],check=True,capture_output=True)
            metrics=read(dest/'metrics.json');previous=read(old/'metrics.json')
            for key in ['EER','FMR100','FMR1000','planned_genuine','planned_impostor']:
                assert metrics[key]==previous[key],(db,condition,key)
            row=dict(db=db,condition=condition,pairs=len(records),templates=len(extracted),max_score_difference=max(r['delta'] for r in records),
                     EER=metrics['EER'],FMR100=metrics['FMR100'],template_mean_bytes=statistics.mean(sizes),template_max_bytes=max(sizes),
                     enroll=stats(enroll_times),attempt=stats(attempt_times),compare=stats([r['call_ms'] for r in records]))
            dump(dest/'summary.json',row);results.append(row)
            all_sizes.extend(sizes);all_enroll.extend(enroll_times);all_attempt.extend(attempt_times);all_compare.extend(r['call_ms'] for r in records)
            total_calls+=len(records);max_delta=max(max_delta,row['max_score_difference'])
            print(db,condition,'parity OK','bytes',round(row['template_mean_bytes']),'compare max ms',round(row['compare']['max_ms'],2),flush=True)
    # Paired serial timing with the same points/fields and same machine, not old logs.
    original=library(ORIGINAL_DLL)
    old_times=[];new_times=[]
    for old,dest,row,ei,ai in samples:
        oa=ct.create_string_buffer((old/'templates'/f'e{ei}.bin').read_bytes());ob=ct.create_string_buffer((old/'templates'/f'a{ai}.bin').read_bytes())
        na=ct.create_string_buffer((dest/'templates'/f'e{ei}.bin').read_bytes());nb=ct.create_string_buffer((dest/'templates'/f'a{ai}.bin').read_bytes())
        for _ in range(2):
            oscore,ms=compare(original,oa,ob);old_times.append(ms)
            nscore,ms=compare(raw,na,nb);new_times.append(ms)
            assert abs(oscore-nscore)<=1e-12
    summary=dict(status='PASS',conditions=results,pairs=total_calls,extractions=len(all_sizes),maximum_score_difference=max_delta,
                 mean_EER=statistics.mean(r['EER'] for r in results),template_mean_bytes=statistics.mean(all_sizes),template_min_bytes=min(all_sizes),template_max_bytes=max(all_sizes),
                 concurrent_workers=4,functions=dict(enroll=stats(all_enroll),attempt=stats(all_attempt),compare=stats(all_compare)),
                 paired_serial_compare=dict(original=stats(old_times),hardened=stats(new_times),speedup=statistics.mean(old_times)/statistics.mean(new_times)),
                 note='Observed timings on this PC only; development-exposed data; no recognition tuning.')
    dump(OUT/'summary.json',summary);dump(OUT/'state.json',dict(status='COMPLETE'))
    print(json.dumps({k:v for k,v in summary.items() if k!='conditions'},indent=2),flush=True)

if __name__=='__main__':run()
