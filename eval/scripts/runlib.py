"""Run a command: returns (rc, wall_s, peak_rss_kB, stdout, stderr).

Peak memory comes from the rssrun launcher (eval/scripts/rssrun.c, Linux:
`cc -O2 eval/scripts/rssrun.c -o rssrun`, then put it on PATH or set RSSRUN).
Without it the command runs directly and peak memory is reported as 0.
"""
import os
import shutil
import subprocess
import time

RSSRUN = os.environ.get('RSSRUN', 'rssrun')
_HAVE = bool(shutil.which(RSSRUN) or os.path.exists(RSSRUN))


def run(cmd):
    if not _HAVE:
        t = time.perf_counter()
        p = subprocess.run(cmd, capture_output=True, text=True, errors='replace')
        return p.returncode, time.perf_counter() - t, 0, p.stdout, p.stderr
    p = subprocess.run([RSSRUN] + cmd, capture_output=True, text=True, errors='replace')
    lines = p.stderr.splitlines()
    last = [l for l in lines if l.startswith('RSSRUN ')][-1].split()
    err = '\n'.join(l for l in lines if not l.startswith('RSSRUN '))
    return p.returncode, float(last[1]), int(last[2]), p.stdout, err
