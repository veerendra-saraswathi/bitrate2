# Plan to close the image and video MVP

**Prepared 11 November 2026, IST.** The dates below are an execution plan, not a promise of full acceptance by 15 November. Implementation defects, unavailable representative data or unresolved acceptance decisions can extend closure. Priorities follow the acceptance matrix, not a target percentage.

## Immediate priorities

| Priority | Work | Reason | Concrete output / exit condition |
|---|---|---|---|
| P0 | Preserve rented-Jetson evidence | Container changed; raw artifacts can be lost | Downloaded metrics/log archive plus verified source/stream/reconstruction backups; per-run identity and hashes |
| P0 | Lock SoW/questionnaire traceability | Prevent PoC scope replacing full MVP | Completed decision register and A01–A15 ownership/status; exact source documents versioned by owner |
| P0 | Fix harness reporting semantics | “Passed checks” currently permits latency misses | Separate correctness, rate, latency, quality and overall acceptance states; unsupported/failed/missing runs never become passes |
| P1 | Independent metric and visual review | Full-scale PSNR and Bayer quality can mislead | Sample-wise error/RMSE/MAE/max, scene-normalized thermal views, worst frames; explicit agreed quality decision |
| P1 | Diagnose CRVD at 2 Mbps | Current high-resolution Bayer fidelity is weak | Controlled baseline reproducer and measured changes with rate/latency/quality comparison; no presumed cause |
| P1 | Expose and implement format gaps | Luma/Bayer are not colour support | Declared interfaces, packing tests and real mono/colour cases for all required depths |
| P1 | Complete high-resolution source coverage | No native approximately 1920×1200 IR evidence yet | Suitable native input from owner/supplier/lab, documented geometry and provenance; written accepted alternative if unavailable |
| P1 | Metadata and actual UDP/Ethernet | Required integration remains open | Field round trip and actual received stream decode; framework-scope decision |
| P1 | Cadence, queues, bandwidth changes and resources | Offline deadline alone does not demonstrate continuous operation | Agreed-duration live/paced test, bounded buffers, no unexplained drops, CPU/GPU/memory records and defined rate-change behavior |
| P1 | Exact-mode acceptance and PC decoding | D+ correct but rate/latency limits substantial | Provided-sample exact results, accepted mode conditions, designated PC output validation and dependency audit |
| P2 | Supplementary Beauty 4/8 Mbps and fair architecture replay | Useful comparison, lower priority than open mandatory gates | Same inputs/configuration and failures reported for all buildable candidates |

## Execution schedule and ownership

Roles are proposed: project owner approves acceptance decisions and data access; implementation engineer changes codec/integration; validation owner records independent evidence. One person may hold multiple roles, but the evidence review must remain explicit.

| Date (IST) | Work package | Dependency | Output | Suggested owner |
|---|---|---|---|---|
| 11 Nov | Evidence preservation, requirement/decision review, input audit, harness status fix | Existing files and source access | Baseline archive; manifests; identity records; A01–A15 register | Validation + project owner |
| 11–12 Nov | Quality review; controlled CRVD investigation; format/colour implementation audit; common still regression set | Verified inputs and metrics | Supported/unsupported format matrix; fixed-window previews; prioritized code changes | Implementation + validation |
| 12–13 Nov | Implement required format handling/quality fixes and metadata round trip; acquire missing data; bounded A–F builds | Previous diagnosis; representative data availability | Tested patches and frozen candidate configuration; missing-input escalation | Implementation + data owner |
| 13 Nov | Jetson replay of agreed matrix; exact/NEAR cases and provided acceptance stills; Beauty 4/8 if time | Frozen candidate, target board, agreed criteria | Per-frame quality/latency/rate and source/binary hashes; explicit failure list | Validation |
| 14 Nov | Actual Ethernet/UDP to designated PC; bandwidth transition, queue/resource capture, integrity and offline audit | Working interfaces and both endpoints | End-to-end receiver evidence and PC build/runtime report | Integration + validation |
| 15 Nov | Close traceability, package releases, compare available architectures, acceptance review | Evidence and unresolved decision resolution | Reviewed acceptance matrix, deliverables §6.1–6.7, candidate tag or explicit blocked status | Project owner + reviewer |
| After 15 Nov if needed | Finish unresolved mandatory implementation/data/quality/integration gates | Nature of failures found | New evidence for open gates; no blanket “final-only” deferral | Assigned owner per gap |

## Verification plan per work package

| Package | Tests that answer a real acceptance question | Stop / reopen condition |
|---|---|---|
| Evidence | Source hashes, full frame counts/order, output accounting, executable and commit identities | Missing originals or uncertain board/configuration; retain status as reported only |
| Rate | Complete encoded-frame bytes and separate UDP/IP/FEC overhead at agreed rates | Any frame/channel budget violation; determine whether channel contract is codec or wire rate |
| Quality | Declared peak, independent RMSE/MAE/max and pooled PSNR; worst-frame visuals with identical display mapping | Threshold not agreed, metric mismatch or unacceptable reconstruction |
| Exact/NEAR | Every supplied acceptance sample compared; configured error bounds on every channel/sample | Any mismatch or bound violation; avoid claiming simultaneous exact 2 Mbps |
| Latency/streaming | Full agreed timing boundary; cold/warm distribution, maximum, misses and queue behavior | Deadline miss or growing queues in required operating envelope |
| Formats | Native or accurately labelled source at all required depths/channels; packing/range/odd geometry/error handling | Unsupported input or changed sample semantics; record transformation explicitly |
| Metadata | Round-trip every agreed field, identifier/time/sequence consistency, unknown/malformed fields | Missing field, wrong units or parser inconsistency |
| Transport | Actual Ethernet endpoints, captured packet configuration, receiver output and loss/reassembly behavior | Simulation-only evidence or corrupted data labelled reliable |
| Delivery | Clean offline build/run on designated encoder and decoder systems; linked dependencies and docs checked | Hidden runtime/download dependency or missing handover item |

## Architecture comparison workstream

| Candidate | Action | Effort rule |
|---|---|---|
| E wavelet | Primary candidate; close current quality/coverage/integration gaps | Allocate most implementation effort where evidence identifies a bottleneck |
| D reference / custom D+ | Compare exactness, NEAR, rate and latency on common sources | Preserve external-library distinction; accept only demonstrated operating modes |
| A | Replay available build on shared mono/native-depth videos | Reconcile historical rate mismatch before ranking |
| B | Bounded rebuild/run; report time and quality failures | Do not spend days reviving broken paths |
| C | Preserve baseline and report supported formats/results | Resume only for a measured advantage |
| F | Preserve corrected exactness and report rate limitation | No renewed entropy-coder project without a measured reason |
| E two-layer / other existing variants | One bounded comparable experiment where buildable | Do not assume an extra layer improves rate, error and time simultaneously |

Architectures are development alternatives. Full MVP does not require every alternative to pass; the selected delivered solution must satisfy the acceptance envelope. Keep the comparison requested by the project owner as separate evidence.
