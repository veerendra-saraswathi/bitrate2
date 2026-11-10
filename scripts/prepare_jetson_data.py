#!/usr/bin/env python3
"""Prepare value-preserving EWC inputs from the user's downloaded archives."""
import argparse, datetime, hashlib, io, json, re, shutil, sys, tarfile, tempfile
from pathlib import Path
from zipfile import ZipFile
import numpy as np
import tifffile
from PIL import Image


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(8*1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def number(name):
    return int(re.search(r'(?:frame_?|_)(\d+)(?:_noisy0)?\.[^.]+$', Path(name).name).group(1))


def write_sequence(out, label, names, read, source, depth, provenance):
    names = sorted(names, key=number)
    if not names:
        raise ValueError(label + ': no frames found')
    ids = [number(n) for n in names]
    if any(b != a+1 for a,b in zip(ids, ids[1:])):
        raise ValueError(label + ': nonconsecutive or duplicate frame IDs')
    target = out / (label + '.u16')
    shape = None
    lo, hi = 65535, 0
    try:
        with target.open('xb') as stream:
            for name in names:
                a = np.asarray(read(name))
                if a.ndim != 2 or a.dtype.kind != 'u' or a.dtype.itemsize > 2:
                    raise ValueError(f'{name}: expected 2D unsigned integer samples, got {a.shape}/{a.dtype}')
                if shape is None:
                    shape = a.shape
                if a.shape != shape:
                    raise ValueError(label + ': changing frame dimensions')
                amin, amax = int(a.min()), int(a.max())
                if amax >= (1 << depth):
                    raise ValueError(f'{name}: sample {amax} exceeds declared {depth}-bit input; refusing to clip')
                lo, hi = min(lo,amin), max(hi,amax)
                stream.write(a.astype('<u2', copy=False).tobytes(order='C'))
        expected = len(names)*shape[0]*shape[1]*2
        if target.stat().st_size != expected:
            raise ValueError('raw size mismatch')
    except Exception:
        target.unlink(missing_ok=True)
        raise
    return dict(id=label, file=target.name, width=shape[1], height=shape[0],
                frames=len(names), first_source_frame=ids[0], last_source_frame=ids[-1],
                bit_depth=depth, storage='uint16 little-endian row-major',
                source_fps=None, test_fps=30, sample_min=lo, sample_max=hi,
                sha256=digest(target), bytes=expected, source=source,
                provenance=provenance, source_members=names,
                transforms='unpack only; no scaling, clipping, denoising, demosaicing or black subtraction')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--downloads', type=Path, default=Path.home()/'Downloads')
    p.add_argument('--output', type=Path, required=True)
    args=p.parse_args()
    if args.output.exists():
        p.error('Output already exists; choose a new folder to avoid overwriting data')
    args.output.mkdir(parents=True)
    manifest=dict(schema=1, created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), preparation_script_sha256=digest(Path(__file__)), scope='First EWC mono/Bayer offline video bundle; not full SoW acceptance',
                  test_fps_note='30 fps is a configured replay reference, not source capture fps',
                  sequences=[], gaps=['Native source fps/timestamps unverified',
                  'High-resolution raw IR near 1920x1200 missing', 'Native 10-bit video not included',
                  'Full color not included', 'Thermal effective sensor precision not established'])
    specs=[('bracken-test-density-raw.tgz','bracken',151,(1024,1024)),
           ('marathon-test-seq1-raw.tgz','marathon',1000,(512,1024))]
    try:
        for archive_name,label,count,geometry in specs:
            archive_path=args.downloads/archive_name
            print('Preparing',label,flush=True)
            with tarfile.open(archive_path,'r:gz') as archive:
                members={m.name:m for m in archive if m.isfile() and m.name.lower().endswith('.png')}
                if len(members)!=count:
                    raise ValueError(f'{label}: expected {count} PNGs, found {len(members)}')
                def read(name):
                    with archive.extractfile(members[name]) as stream:
                        with Image.open(stream) as im:
                            a=np.array(im)
                            # Pillow may expose 16-bit PNG as signed int32; validate before converting.
                            if a.dtype.kind=='i' and a.min()>=0 and a.max()<=65535:
                                a=a.astype(np.uint16)
                            return a
                source=dict(archive=archive_name, archive_sha256=digest(archive_path))
                result=write_sequence(args.output,label,list(members),read,source,16,
                    'BU-TIV raw thermal; 16-bit PNG storage; effective sensor depth unverified')
                if (result['height'],result['width'])!=geometry:
                    raise ValueError(label+': unexpected geometry')
                manifest['sequences'].append(result)
        candidates=list(args.downloads.glob('indoor_raw_noisy*.zip'))
        candidates=sorted(candidates,key=lambda x:x.stat().st_mtime,reverse=True)
        chosen=None
        for path in candidates:
            with ZipFile(path) as outer:
                found=[n for n in outer.namelist() if Path(n).name=='indoor_raw_noisy_scene6.zip']
                if found:
                    chosen=(path,found[0]);break
        if chosen is None:
            raise ValueError('Cannot locate nested indoor_raw_noisy_scene6.zip in Downloads')
        path,member=chosen
        print('Preparing CRVD scene6, lowest available ISO, noisy0 track',flush=True)
        with ZipFile(path) as outer, tempfile.TemporaryFile() as temp:
            with outer.open(member) as stream:
                shutil.copyfileobj(stream,temp)
            temp.seek(0)
            with ZipFile(temp) as inner:
                names=[n for n in inner.namelist() if re.search(r'scene6/ISO\d+/frame\d+_noisy0\.tiff$',n)]
                isos=sorted(set(int(re.search(r'/ISO(\d+)/',n).group(1)) for n in names))
                if not isos:
                    raise ValueError('CRVD noisy0 track absent')
                iso=isos[0]
                names=[n for n in names if f'/ISO{iso}/' in n]
                def read_tiff(name):
                    return tifffile.imread(io.BytesIO(inner.read(name)))
                source=dict(archive=path.name,archive_sha256=digest(path),nested_archive=member,iso=iso)
                result=write_sequence(args.output,f'crvd_scene6_iso{iso}_noisy0',names,
                    read_tiff,source,12,'CRVD captured Bayer scene6; noisy0 track; capture timing/method must remain documented')
                result['cfa']='GBRG per CRVD authors; verify against selected file metadata'
                result['note']='One short capture group; do not join noisy variants, ISO groups or clean references'
                manifest['sequences'].append(result)
        runner=Path(__file__).with_name('run_jetson_video.py')
        shutil.copyfile(runner,args.output/runner.name)
        (args.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        (args.output/'README.txt').write_text(
            'Value-preserving uint16 little-endian inputs. Source archives retained on Mac.\n'
            'Thermal bit_depth=16 describes the codec input/storage, not measured sensor precision.\n'
            'Bayer is not native monochrome. Source fps is unknown; test fps=30 is a reference.\n'
            'Jetson: python3 run_jetson_video.py --repo /path/to/ombitrate\n'
            'Default: 10-frame tiled 2/4/8 Mbps smoke tests with 128-byte metadata.\n'
            'Full sequences: add --frames 0. D+ checks: add --include-exact.\n'
            'Runner measures offline codec timing, not live UDP delivery or queue growth.\n')
        tarpath=args.output.with_name(args.output.name+'.tar')
        if tarpath.exists():
            raise ValueError('Transfer TAR already exists; refusing overwrite')
        print('Creating transfer TAR (uncompressed, to avoid a long compression delay)',flush=True)
        with tarfile.open(tarpath,'w') as tar:
            tar.add(args.output,arcname=args.output.name)
        checksum=digest(tarpath)
        tarpath.with_name(tarpath.name+'.sha256').write_text(checksum+'  '+tarpath.name+'\n')
        print('READY:',tarpath)
        print('SHA256:',checksum)
        for s in manifest['sequences']:
            print(s['id'],s['width'],s['height'],s['frames'],'frames',s['bit_depth'],'codec bits')
    except Exception:
        print('PREPARATION FAILED. Partial folder retained for inspection; do not run it.',file=sys.stderr)
        raise

if __name__=='__main__':main()
