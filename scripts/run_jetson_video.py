#!/usr/bin/env python3
"""Offline EWC tiled and optional D+ checks; Python standard library only."""
import argparse, csv, datetime, hashlib, json, math, platform, shutil, subprocess, time
from pathlib import Path


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(8*1024*1024),b''):h.update(block)
    return h.hexdigest()


def capture(cmd):
    try:
        r=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=20)
        return dict(command=cmd,returncode=r.returncode,output=r.stdout)
    except Exception as e:return dict(command=cmd,error=str(e))


def run(cmd,log,commands):
    commands.append(cmd)
    start=time.monotonic()
    with log.open('w') as f:
        r=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT)
    if r.returncode:raise RuntimeError(f'Command failed ({r.returncode}); see {log}')
    return time.monotonic()-start


def rows(path):
    with path.open() as f:return list(csv.DictReader(f))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo',type=Path,required=True)
    p.add_argument('--frames',type=int,default=10,help='0=all frames; default 10 for smoke checks')
    p.add_argument('--threads',type=int,default=2)
    p.add_argument('--fps',type=float,default=30)
    p.add_argument('--include-exact',action='store_true',help='also run D+ exact and NEAR=4')
    args=p.parse_args()
    if args.frames<0 or args.threads<1 or args.fps<=0:p.error('Invalid frames/threads/fps')
    base=Path(__file__).resolve().parent
    manifest=json.loads((base/'manifest.json').read_text())
    repo=args.repo.resolve()
    enc,dec=repo/'build/ewc_enc',repo/'build/ewc_dec'
    for path in [enc,dec]:
        if not path.is_file():p.error(f'Missing executable: {path}. Build the repo first.')
    # Verify every staged input before running any codec operation.
    for s in manifest['sequences']:
        path=base/s['file']
        if path.stat().st_size!=s['bytes'] or digest(path)!=s['sha256']:
            raise ValueError('Input verification failed: '+s['id'])
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    result=base/('results_'+stamp)
    result.mkdir(exist_ok=False)
    hardware=dict(utc=stamp,platform=platform.platform(),machine=platform.machine(),
                  test_fps=args.fps,threads=args.threads,metadata_bytes=128,
                  metadata_note='128 ASCII bytes serialized with --meta; metadata parsing/round-trip not independently verified',
                  commands=[capture(['git','-C',str(repo),'rev-parse','HEAD']),
                  capture(['git','-C',str(repo),'status','--porcelain']),capture(['uname','-a']),
                  capture(['nvpmodel','-q']),capture(['jetson_clocks','--show'])],
                  binaries={str(x):digest(x) for x in [enc,dec]})
    for name in ['/proc/device-tree/model','/etc/nv_tegra_release']:
        path=Path(name)
        if path.exists():hardware[name]=path.read_text(errors='replace').replace('\x00','').strip()
    (result/'environment.json').write_text(json.dumps(hardware,indent=2)+'\n')
    shutil.copyfile(base/'manifest.json',result/'input_manifest.json')
    commands=[];summaries=[];failures=[]
    telemetry=None
    if shutil.which('tegrastats'):
        telemetry=subprocess.Popen(['tegrastats','--interval','1000'],stdout=(result/'tegrastats.log').open('w'),stderr=subprocess.STDOUT)
    try:
        for s in manifest['sequences']:
            n=min(args.frames,s['frames']) if args.frames else s['frames']
            modes=[('wavelet_2m',2000,None),('wavelet_4m',4000,None),('wavelet_8m',8000,None)]
            if args.include_exact:modes += [('dplus_exact',None,0),('dplus_near4',None,4)]
            # A reference prefix prevents accidental comparison of a partial output with a full source.
            ref=result/(s['id']+'_reference.u16')
            expected=n*s['width']*s['height']*2
            with (base/s['file']).open('rb') as f,ref.open('wb') as g:
                remaining=expected
                while remaining:
                    block=f.read(min(8*1024*1024,remaining))
                    if not block:raise ValueError('Truncated input')
                    g.write(block);remaining-=len(block)
            for label,kbps,near in modes:
                key=s['id']+'_'+label
                prefix=result/key
                stream=prefix.with_suffix('.ewc'); rec=prefix.with_suffix('.u16')
                enc_csv=prefix.with_suffix('.enc.csv');dec_csv=prefix.with_suffix('.dec.csv')
                mode=['--tile','128','--tile-levels','4','--gop','1','--kbps',str(kbps)] if near is None else ['--lossless','--near',str(near)]
                cmd=[str(enc),'-i',str(ref),'-o',str(stream),'-W',str(s['width']),'-H',str(s['height']),
                     '--bd',str(s['bit_depth']),'--threads',str(args.threads),'--fps',str(args.fps),
                     '--frames',str(n),'--meta','M'*128,'--stats',str(enc_csv)]+mode
                print('RUN',key,n,'frames',flush=True)
                try:
                    wall=run(cmd,prefix.with_suffix('.enc.log'),commands)
                    run([str(dec),'-i',str(stream),'-o',str(rec),'--ref',str(ref),'--report',str(dec_csv),
                         '--frames',str(n),'--threads',str(args.threads)],prefix.with_suffix('.dec.log'),commands)
                    er,dr=rows(enc_csv),rows(dec_csv)
                    if len(er)!=n or len(dr)!=n or rec.stat().st_size!=expected:raise ValueError('Frame count/output size mismatch')
                    if any(int(r['frame'])!=i or int(r['seq'])!=i or int(r['reliable'])!=1 or int(r['concealed']) for i,r in enumerate(dr)):
                        raise ValueError('Frame order, reliability or concealment failure')
                    times=sorted(float(r['encode_ms']) for r in er)
                    decode_times=sorted(float(r['decode_ms']) for r in dr)
                    errors=[int(r['maxerr']) for r in dr]
                    if near is not None and max(errors)>near:raise ValueError('NEAR bound exceeded')
                    if near==0 and digest(ref)!=digest(rec):raise ValueError('Exact mode is not bit-exact')
                    sizes=[int(r['bytes']) for r in er]
                    if sum(sizes)!=stream.stat().st_size:raise ValueError('Encoded byte accounting mismatch')
                    budget=int(kbps*1000/8/args.fps) if kbps else None
                    rate_ok=budget is None or max(sizes)<=budget
                    if not rate_ok:raise ValueError('Per-frame byte budget exceeded')
                    def percentile(values,q):return values[round((len(values)-1)*q)]
                    psnr=['infinite' if e==0 else r['psnr'] for e,r in zip(errors,dr)]
                    summaries.append(dict(sequence=s['id'],mode=label,frames=n,width=s['width'],height=s['height'],
                        codec_bit_depth=s['bit_depth'],fps_reference=args.fps,actual_mbps=sum(sizes)*8*args.fps/n/1e6,
                        bytes_max=max(sizes),budget_bytes=budget,enc_median_ms=percentile(times,.5),
                        enc_p95_ms=percentile(times,.95),enc_p99_ms=percentile(times,.99),enc_max_ms=max(times),
                        encoder_deadline_misses=sum(t>2000/args.fps for t in times),dec_median_ms=percentile(decode_times,.5),
                        dec_max_ms=max(decode_times),maxerr=max(errors),
                        psnr_frame_mean_db='infinite' if 'infinite' in psnr else sum(map(float,psnr))/n,
                        psnr_note='Per-frame decoder-reported PSNR; zero-error sentinel normalized to infinite; peak=(2^codec_bit_depth)-1',
                        encode_process_wall_s=wall,status='PASS_RECORDED_CHECKS',
                        limitation='Offline file replay; no live throughput/queue or UDP acceptance claim'))
                    # Keep reconstructed samples and CSVs for independent review.
                except Exception as e:
                    failures.append(dict(run=key,error=str(e)))
                    print('FAILED',key,str(e),flush=True)
                (result/'summary.json').write_text(json.dumps(dict(runs=summaries,failures=failures),indent=2)+'\n')
                (result/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    finally:
        if telemetry:
            telemetry.terminate()
            try:telemetry.wait(timeout=5)
            except subprocess.TimeoutExpired:telemetry.kill();telemetry.wait()
    print('RESULTS:',result)
    print('Passed checks:',len(summaries),'Failed:',len(failures))
    if failures:raise SystemExit(1)

if __name__=='__main__':main()
