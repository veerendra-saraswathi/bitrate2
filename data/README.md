# Test data

All results in this repo were measured on exactly these files.

## 1. FLIR ADAS v2, 14-bit (main test set) — not in the repo (≈650 MB)

Packed from the FLIR ADAS v2 release with `eval/flir_prep/pack.py` (values copied as stored,
no scaling). On Sam's PC they are already in:

`C:\Users\omjin\Downloads\Bitrate-20261026T054640Z-1-001\FLIR_ADAS_v2_raw16\`

| File | Frames | Content | MD5 |
|---|---|---|---|
| flirv_4FRnNp_640x512_150.u16 | 150 | video, frames 745–894 | 873319deafe852967b6cdf8ea508c6c3 |
| flirv_5RSrbW_640x512_150.u16 | 150 | video, frames 99–248 | 7b9059e0cb4795528bff2ede4d8e2617 |
| flirv_6tLtjd_640x512_150.u16 | 150 | video, frames 0–149 | 29542932196441758c72c62fe5d33da4 |
| flirv_SCiKdG_640x512_150.u16 | 150 | video | 6c584e173f75e0b519f104f61817ec96 |
| flirv_ePoikf_640x512_150.u16 | 150 | video | ff99dafbe83c60d94c6361da15ee86ce |
| flirv_vbrSzr_640x512_150.u16 | 150 | video | 7ad6a7ea9e7a34da7d84c2638c205e54 |
| flirstill_val_640x512_100.u16 | 100 | random val stills (seed 7) | 1eadacc794b749d6f6da54d53138ccfe |

Frame lists and value ranges: `data/flir_manifest.json`. Format: raw little-endian uint16,
640×512, frames back to back.

To use them, either copy the files into `data/flir/`, or point the scripts at the folder:

```bash
export EWC_DATA=/path/to/FLIR_ADAS_v2_raw16     # Windows: set EWC_DATA=C:\...\FLIR_ADAS_v2_raw16
export EWC_BIN=build                            # where ewc_bench / ewc_enc / ewc_layered were built
```

To rebuild them from the FLIR release: `python3 eval/flir_prep/pack.py <FLIR_ADAS_v2> <out>`.

## 2. KAIST 640×512, 8-bit — in the repo

`data/kaist/kaist_thermal.raw`, `data/kaist/kaist_visible.raw`: the bitrate-main comparison
frames (raw uint8, 640×512, one frame each). Architecture D's published numbers were
reproduced byte-exact on these.

| File | MD5 |
|---|---|
| kaist_thermal.raw | 48e5d36664696bfd407edbb99ab326bb |
| kaist_visible.raw | 6ef24f7f6e44fb87283902d77f312b9a |

## 3. Extra tools some scripts need

- `pip install numpy scikit-image imagecodecs` — imagecodecs provides CharLS (Architecture D) for the comparison scripts only.
- Architecture A numbers need bitrate-main's library: build `eval/h2h_v2/bm_eval.cpp` against it and run `eval/h2h_v2/run14.sh`.
- HTJ2K scripts need OpenJPH apps: `export OPENJPH_APPS=/path/to/OpenJPH/build/src/apps`.

## 4. Older corpora (first phase only)

`eval/scripts/lossless_check.py`, `resources.py` and `stats.py` also reference the first-phase
test clips (pv24, pan90, csq7, seq28 and FLIR SEQ/CSQ sources). They are not used by any
result in `docs/ARCHITECTURES_EXPLAINED.md` or the slide deck, and are not on this PC.
