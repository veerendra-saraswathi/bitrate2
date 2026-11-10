# Repository layout and local reconciliation

**11 November 2026, IST.** Keep the project easy to navigate without moving code paths that CMake/scripts depend on. This first cleanup reorganizes navigation, not the encoder implementation.

## Current canonical folder roles

| Path | Role | Git policy |
|---|---|---|
| `README.md` | Short landing page; one achieved/remaining table | Curated, current |
| `include/`, `src/` | Public interfaces and encoder/decoder implementation | Tracked source; unchanged by cleanup |
| `tools/`, `tests/`, `CMakeLists.txt` | CLI tools, tests and build definition | Tracked source; unchanged by cleanup |
| `scripts/` | Reusable preparation, run and scoring entry points | Tracked; source/configuration is evidence |
| `docs/` | Algorithm, bitstream, network and historical reports | Preserve canonical links; index at docs/README.md |
| `docs/mvp/` | Current status, acceptance, plan, coverage and evidence index | Canonical scope/status documents |
| `docs/results/jetson_2026-11-11/` | Supplied aggregate JSON reports | Curated small evidence, not raw video |
| `docs/slides/` | Presentation assets | Canonical deck location; identical root duplicate removed in cleanup commit |
| `eval/` | Existing experimental scripts, references, curated metrics/plots | Preserve historical experiment context; do not bulk-delete measurements |
| `data/` | Dataset README, manifests, small historical fixtures | New large downloads/raw sequences ignored by default; existing tracked data retained |
| `build/`, `score_out/`, `poc_out/`, transient `results_*/` | Local build/run products | Ignored; copy selected evidence into curated paths explicitly |
| Dataset/result artifact storage outside source Git | Original datasets, streams, full reconstructions | Hashes/manifests and controlled links; respect redistribution terms |

## What the supplied Mac tree actually shows

| Local path | Observed content | Meaning |
|---|---|---|
| `~/bitrate2/ombitrate/` | CMakeLists, README, include/src/tools/tests, docs/eval, build and older results | This is the source checkout location shown in the tree. Verify Git root/remotes before moving it |
| `~/bitrate2/data/` | FLIR clips and UVG symlink | Outer workspace data; not evidence of a second complete source checkout |
| `~/bitrate2/docs/` | New dated Jetson report and aggregate JSONs | Some earlier documentation was installed outside the nested source folder |
| `~/bitrate2/jetson_results/` | A Beauty result directory | Historical evidence; do not remove during folder cleanup |
| Jetson `~/bitrate2/` | Flat source checkout and current results | Different local layout; don't assume Mac and Jetson paths match |

This explains the earlier `docs/ARCHITECTURES_EXPLAINED.md` missing-file error in the outer Mac folder.

## Does ombitrate go?

Use **Bitrate2** as the project/repository name and **ewc** as the codec/API name. The local folder name `ombitrate` can eventually be retired, but it is currently where the supplied Mac source tree lives. **Do not delete it.** No nested `ombitrate/` directory is tracked at the root of the inspected GitHub branches, so there is no such remote folder to remove.

Before any local consolidation, run these read-only checks on the Mac:

```bash
git -C "$HOME/bitrate2/ombitrate" rev-parse --show-toplevel
git -C "$HOME/bitrate2/ombitrate" remote -v
git -C "$HOME/bitrate2/ombitrate" status --short --branch
git -C "$HOME/bitrate2" rev-parse --show-toplevel
```

The final command may fail if the outer directory is only a workspace. Record that; do not initialize another Git repository just to make it succeed.

| Stage | Action | Gate before proceeding |
|---|---|---|
| 1: inventory | Record each actual Git root, branch/remotes and uncommitted changes; inventory outer data/docs/results | No source/data guessed from folder names |
| 2: preserve | Back up local edits, curated result originals, manifests and large datasets; verify copy/hash where appropriate | Backup can be located and checked; local-only branches are not the sole copy |
| 3: reconcile docs | Apply this cleanup to the real checkout and copy/compare outer new reports | Preserve conflicting versions with provenance; no blind recursive overwrite |
| 4: standardize | Prefer a clean flat `~/bitrate2` checkout eventually, with large datasets stored separately | Verify source revision, data paths and links before retiring the older nested folder |
| 5: retire duplicate workspace | Rename/archive old folders only after a clean build and result-path check | Separate explicit cleanup; not performed by this branch |

Do not move the existing CMake build directory into a new checkout: its cache contains absolute paths. Reconfigure a fresh build. Moving symlinks can also invalidate relative targets. No local Mac data or folders were moved by the remote documentation cleanup.

## Ignore policy

Build products, raw datasets, reconstructed media, split raw files, transient run directories, Python caches and backup files are ignored. Curated CSV/PNG evidence is not globally hidden: the previous blanket `*.csv`/`*.png` ignores were replaced by directory-oriented output rules. Already tracked files remain tracked; `.gitignore` is not a history-cleaning tool.
