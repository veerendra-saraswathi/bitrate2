#!/bin/sh
# --poc --lossless must fall back to whole-frame lossless and round-trip bit-exactly;
# --tile together with --lossless must be rejected.
set -e
B="$1"; T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
python3 -c "import random,struct,sys;random.seed(7);sys.stdout.buffer.write(struct.pack('<%dH'%(640*512*3),*[random.randrange(16384) if i%97==0 else (i%640)*25 for i in range(640*512*3)]))" > "$T/in.raw"
"$B/ewc_enc" --poc --lossless -i "$T/in.raw" -o "$T/o.ewc" 2>/dev/null
"$B/ewc_dec" -i "$T/o.ewc" -o "$T/o.raw" >/dev/null 2>&1
cmp "$T/in.raw" "$T/o.raw"
if "$B/ewc_enc" --poc --tile 64 --lossless -i "$T/in.raw" -o "$T/x.ewc" 2>/dev/null; then echo "tile+lossless accepted"; exit 1; fi
echo PASS
