# Evidence index and reproducibility rules

**11 November 2026, IST.** This repository contains reported aggregate JSONs and dated narrative. Original per-frame CSVs, streams and reconstructions remain on the rented Jetson unless separately archived. Publishing this documentation does not upload those assets or establish final acceptance.

| Evidence | Location / identity | Level and scope |
|---|---|---|
| Latest detailed Jetson report | [JETSON_VIDEO_RESULTS_2026-11-11.md](../JETSON_VIDEO_RESULTS_2026-11-11.md) | User-supplied completed-run aggregate results and limitations |
| Full-set aggregate | [full_sequence_summary_reported.json](../results/jetson_2026-11-11/full_sequence_summary_reported.json) | 15 cases, Bracken/Marathon/CRVD; original per-frame CSVs not embedded |
| FLIR aggregate | [flir_summary_reported.json](../results/jetson_2026-11-11/flir_summary_reported.json) | 18 cases, six clips × three rates |
| Earlier image/PoC report | [POC_REPORT.md](../POC_REPORT.md) | Earlier configuration/host; source-scope and timing caveats retained |
| Architecture history | [ARCHITECTURES_EXPLAINED.md](../ARCHITECTURES_EXPLAINED.md) | Earlier still/video observations, not a fresh fair ranking |
| Format and transport | [FORMAT.md](../FORMAT.md), [NETWORK.md](../NETWORK.md) | Interface descriptions; existence is not end-to-end test acceptance |
| Source hierarchy | [ACCEPTANCE.md](ACCEPTANCE.md) | Full SoW §§1–7 and appended clarifications re-read; 2 Mbps primary reaffirmed by user |

| Run | UTC start | IST start (11 Nov) | Original directory relative to Jetson home |
|---|---|---|---|
| Full set | 2026-11-10 18:43:39Z | 00:13:39 | `jetson_video_inputs_20261110/results_20261110T184339Z/` |
| Beauty 2 Mbps | 2026-11-10 19:00:02Z | 00:30:02 | `bitrate2/results/beauty_2m_20261110T190002Z/` |
| FLIR sweep | 2026-11-10 19:36:42Z | 01:06:42 | `bitrate2/data/flir/jetson_test_20261110T193640Z/results_20261110T193642Z/` |

| Identity item | Current record |
|---|---|
| Initial tested revision | `ab31fda985392dc6baa92b7a8f9759e727b54cea` reported by Jetson and Beauty; do not assume it equals public main |
| Initial dirty state | Modified `scripts/score_top3.sh`; untracked Beauty input and score output |
| Initial encoder SHA256 | `29c182de96e1974c020a26fd5480eb379988551b864b3d003ed22d53a5b7a9f2` |
| Initial decoder SHA256 | `17a33162e3b8bdb21b8e2ddc714e073d2d03ed546b36ecf5ad1db9f2e34ce256` |
| Beauty input SHA256 | `4cbe16d59503b3dedb281c1413cf515b8cec5bb9b9657c89185754a200d6da61` |
| FLIR | Mac/Jetson per-file SHA256 comparisons all OK; retain actual checksum file and per-run input manifest |
| Container distinction | First: `8bc983542a1a`; FLIR: `cb1b0c61b75c`; board model checked in both, clock/power identity unresolved |
| Documentation base reviewed | Public `origin/main` fetched for this update: `f07161b`; local documentation additions are separate from tested code |

## Preserve compact evidence on Jetson

This command excludes raw samples and encoded streams; preserve those separately if needed. It fails if a named run directory has disappeared, which must be resolved rather than silently omitted.

```bash
tar --exclude='*.u16' --exclude='*.ewc' \
  -czf "$HOME/jetson_video_metrics_20261111.tar.gz" -C "$HOME" \
  jetson_video_inputs_20261110/results_20261110T184339Z \
  bitrate2/results/beauty_2m_20261110T190002Z \
  bitrate2/data/flir/jetson_test_20261110T193640Z/results_20261110T193642Z
sha256sum "$HOME/jetson_video_metrics_20261111.tar.gz"
```

Download and verify this archive before releasing the machine. Do not publish source datasets without checking redistribution permissions. Large reconstructions/streams belong in an appropriately controlled artifact store, with stable hashes and links in the report, rather than ordinary Git history.

## Rules for future results

| Record | Minimum content |
|---|---|
| Input manifest | Source/provenance, geometry, true/storage depth, channels/packing, frame indices/cadence, transformations, hashes |
| Build/platform | Source revision and dirty diff, compiler/flags, binary hashes, exact model/OS, power/clocks, thread counts |
| Invocation | Complete command/config, profile, tiles/levels, GOP, rate, fps, metadata and error protection |
| Metrics | Complete encoded bytes, codec and wire rate, compression ratio basis, peak definition, pooled errors, max error, per-frame encode/decode timings |
| Acceptance states | Separate correctness, budget, encoder deadline, quality, metadata, transport and whole-envelope acceptance |
| Failures | Missing/empty outputs, unsupported formats, deadline misses and invalid metrics remain explicit |
| Comparisons | Same input samples and measurement contract; no cross-peak or cross-host ranking without qualification |

Raw aggregate values are preserved as supplied; do not synthesize missing per-frame CSVs from averages. The current runner's `PASS_RECORDED_CHECKS` does not include a mandatory latency failure gate or a quality threshold. Report those separately until the harness is corrected.
