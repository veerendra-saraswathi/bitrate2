# Dataset coverage: downloaded, prepared, tested and missing

**11 November 2026, IST.** Filenames, container bits and source appearance are not proof of native precision or video cadence. Only measured codec runs count as tested.

| Data | Confirmed possession / preparation | Codec evidence | Remaining use / limitation |
|---|---|---|---|
| FLIR selected raw clips | Six Mac `.u16` files transferred to Jetson; SHA256 matched; each 98,304,000 B / 150 frames / 640×512 | 900 source frames at 2/4/8 Mbps on Jetson; 14-bit profile | Preserve original TIFF provenance and selected-frame manifest; not high-resolution IR |
| FLIR validation stills | Manifest lists 100 frames; Drive contains `flirstill_val_640x512_100.u16` | Earlier host PoC report covers still set | Latest Jetson transfer/test not confirmed; do not add 100 to current Jetson frame count |
| Bracken | 151 gap-free 1024×1024 gray 16-bit PNGs; prepared u16 | Full Jetson 2/4/8 + D+ exact/NEAR | Audit native sensor precision; source cadence not established |
| Marathon | 1000 gap-free 1024×512 gray 16-bit PNGs; prepared u16 | Full Jetson 2/4/8 + D+ exact/NEAR | Same provenance/precision/cadence caveats |
| CRVD indoor scene6 | Nested ZIP: 385 TIFFs include ISO/clean/noisy variants; prepared ISO1600 noisy0 track, seven 1920×1080 positions | Jetson 12-bit Bayer profile, 2/4/8 + D+ | Only seven positions; variants are not extra temporal frames; quality and full-colour path unresolved |
| Beauty luma | 50 frames, 1920×1080, 8-bit luma stored u16; verified size/range/hash | Current Jetson 2 Mbps + metadata; earlier other-rate runs recorded separately | Need longer original, matched current 4/8 replay; luma is not colour |
| WB clean postprocessed | Archive: 3000 TIFFs / 120 named scene folders / 25 each | Not established as codec-tested by latest runs | Audit TIFF channels/depth/source; names Beauty/Bosphorus/SunBath do not prove original UVG/native data |
| `vi-20261110T141450Z-1-003.zip` | 2506 JPGs in `vi` | No codec run established | No native high-depth claim; provenance/order/sequence continuity pending |
| `archive (1).zip` | 114 thermal electrical JPEG stills | No codec run established | Supplementary stills, not established raw thermal video; embedded radiometric content unverified |
| Canon S100 DNG | Thumbnail plus 12-bit CFA raw SubIFD inspected | No codec run established | Useful native-12 still candidate; decode raw IFD and preserve values; not video |
| Blackmagic DNG samples | Pocket/Micro/Pocket4K/URSA raw metadata inspected; some have 4096-entry linearization tables | No codec run established | Need decode/linearization audit; 12-bit stored codes do not mean linear uint12 samples |
| ShakeNDry UVG archive | Previously downloaded 1080p120 8-bit YUV archive | Extraction and current test not confirmed | Useful motion/texture and processed colour coverage once prepared |
| KAIST regression stills | Earlier 640×512 8-bit visible/thermal frames | Host/single-frame baseline | Widening JPG samples does not create native 14-bit sensor data |
| LTIR selected frames | Earlier five stored-16-bit 640×480 frames reported | Prior short host sequence | Full source corpus/effective precision and new Jetson test not established |

## Missing coverage in priority order

| Priority | Needed coverage | Acquisition / preparation action | Evidence to preserve |
|---|---|---|---|
| P1 | Representative approximately 1920×1200 VIS/IR | Obtain real suitably sized raw source from camera owner, supplier or lab; agree an alternative if unavailable | Sensor/source identity, unscaled geometry, fps, frame indices and packing |
| P1 | Actual colour reconstruction | Prepare existing legitimate YUV/RGB source; implement agreed channel handling | Colour model, plane order, chroma subsampling, per-plane error and reconstructed output |
| P1 | Native 10-bit video | Obtain documented 10-bit source; an official UVG 10-bit sequence is processed YUV, not Bayer | Original source/hash; crop if needed, no upscaling; transformations labelled |
| P1 | Longer raw visible 12-bit sequence | Request camera/supplier capture; use CRVD only as a labelled seven-position test | Packing, effective precision, exposure, geometry and cadence |
| P1 | Documented effective-16-bit source | Audit Bracken/Marathon/LTIR provenance first; request capture only if audit cannot establish precision | Sensor/processing provenance; distinguish storage from effective precision |
| P2 | Broader motion, texture, low contrast and scene changes | Inspect existing 120-scene archive and UVG downloads before obtaining more | Scene-separated development/evaluation split and sequence continuity |

Do not require every spectral band or multispectral multiplexing merely to expand the corpus. Sequence lengths and test durations are engineering choices until acceptance conditions are agreed.
