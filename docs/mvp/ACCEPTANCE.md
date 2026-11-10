# Full MVP acceptance: SoW traceability and exit gates

**As of 11 November 2026, IST. Full-scope MVP = the smallest deliverable that satisfies every mandatory agreed SoW gate.** A weekly demonstrator can have explicitly documented gaps, but cannot be called full acceptance while those remain open. No weighted score overrides a failed mandatory gate.

## Source authority and unresolved decisions

| Source | Authority and treatment |
|---|---|
| S1: supplied full SoW, “Low-Bitrate, High-Resolution Image/Video Compression with Low Latency,” sections 1–7 | Governs mandatory scope. Re-read from supplied `Pasted markdown(8).md` for this update. Requirement wording below is a traceable paraphrase, not a new contract. |
| S2: clarifications appended to S1 | Single stream; initial VIS/IR samples described as monochrome/raw; provisional 128-byte metadata. Colour in S1 remains visible. “Integration not required” conflicts with S1 §7 framework wording and requires a written resolution. |
| S3: supplied questionnaire/PoC operating point, user reaffirmed in this session; repository PoC report | 2 Mbps primary. The exact applicability to final resolutions/formats must be confirmed; no universal full-SoW quality threshold is invented. |
| S4: user engineering decisions | Also test 4/8 Mbps; compare every buildable architecture, bound effort on broken paths; file plus actual UDP path is a weekly target. These choices do not waive S1 gates. |
| S5: reported runs and repository records | Evidence only within the recorded source/configuration/platform. Older results are not new tests. |

| Decision requiring agreement | Current working value / issue | Required recorded decision |
|---|---|---|
| Operating rate and cadence | 2 Mbps primary; 4/8 Mbps supplementary; 30 fps reference | Accepted rate-resolution-format-fps matrix; define codec bytes versus on-wire channel budget |
| Quality | Quantitative and visual evaluation required; no final minimum recorded | Numerical and visual criteria for specified input classes and bitrates |
| Perfect reconstruction | Explicit in S1 §7; D+ exact works but at much higher rates | Acceptance sample hashes, allowed mode(s), and whether exactness/rate must be simultaneous |
| Formats and geometry | Indicative approximately 1920×1200; 8/10/12/14/16 mono/colour | Supported input representations, dimensions and real coverage without silently deleting S1 formats |
| Latency timing boundary | Agreed input availability to corresponding complete output | Capture/queue/copy/rate-search/output boundaries, fps, power mode, run duration and deadline policy |
| Metadata | Provisional 128-byte application allowance | Field schema, types, units, timestamp semantics, versioning and error behavior |
| Decoder OS | S1 §2 says Linux and Windows; §6.3 says Linux and/or Windows | Designated PC OS/executables; retain both as pending unless formally narrowed |
| Dependencies | Self-contained C/C++; no external libraries/runtime wording | Approved system libraries and interpretation of vendored/static third-party code |
| UDP framework integration | S1 §7 names framework; appended answer says not required | Whether clean standard UDP interface suffices or actual framework integration is needed |

## Requirement-by-requirement acceptance matrix

Status meanings: **Demonstrated subset** = current evidence passes a bounded case; **Partial** = implementation/evidence exists with gaps; **Open** = needed evidence absent; **Needs agreement** = acceptance conditions not settled. No row below is a blanket full-scope pass.

| ID | Requirement / source | Evidence now | Status | Exit evidence for full acceptance |
|---|---|---|---|---|
| A01 | Defined operating envelope and acceptance criteria; S1 §§5,7 | Public datasets and reference settings chosen | Needs agreement | Versioned sample manifest, formats, fps, bitrates, quality/error thresholds, timing boundaries and approved deviations |
| A02 | Visible + IR, approximately 1920×1200 and agreed smaller sizes; §§1,3.1,4 | Real 640×512 FLIR; 1024 thermal; 1920×1080 Beauty/Bayer | Partial | Agreed representative VIS/IR geometry matrix; no upscaled low-resolution source counted as native high-resolution IR |
| A03 | 8/10/12/14/16-bit mono and colour; §§1,2,3.1 | Mono/luma and Bayer sample paths; 8/12/14/16 codec profiles exercised | Partial | Correct input/output at each required depth and channel layout, range/packing validation, provenance and native-depth coverage; actual colour reconstruction |
| A04 | Perfect reconstruction of supplied sample images; §§5,7 | D+ exact on all 1,158 Bracken/Marathon/CRVD samples; historical still exactness | Demonstrated subset | Byte/sample-exact comparison on the actual supplied acceptance images/formats and supported exact mode; document rate/latency compatibility |
| A05 | Quality/error at specified operating bitrates; §§2,5,7 | FLIR/Beauty metrics; CRVD 2 Mbps poor; no final threshold | Needs agreement / partial | Agreed quality criterion passes per class, declared peaks, pooled errors, max error, worst-frame matched visual review; no sentinel PSNR |
| A06 | Encoder ≤2 frames; §§3.5,7 | At 2 Mbps 0/2,108 measured deadline misses at 30 fps | Demonstrated subset | All agreed cases pass ≤2000/fps ms under documented complete timing boundary; distributions/max/misses and sustained behavior retained |
| A07 | Configurable bitrate and target-rate behavior; §§3.1,3.3,7 | 2/4/8 Mbps sweeps; whole encoded-frame budget checks | Demonstrated subset | All agreed bitrate cases, complete byte accounting, compression ratio and defined channel-overhead budget pass |
| A08 | Self-contained bitstream and per-frame metadata parsing; §§3.2,3.4,6.4,7 | Metadata inserted as 128 ASCII bytes, internal frame metadata exists | Partial | Required fields round-trip exactly; identifiers/timestamps/order verified; standalone parsing and malformed/missing metadata checks |
| A09 | File storage, Ethernet over standard UDP and agreed interface; §§2,3.2,6.4,7 | Files and decoder outputs work; packetizer/simulator and code exist | Partial | Actual sender/receiver Ethernet test, reassembly, file replay, decoded output validation; resolve framework scope |
| A10 | Operation under varying available bandwidth; §§3.3,5,6.5,7 | Fixed-rate selections work | Partial | Documented selection/adaptation mechanism; controlled bandwidth change with measured quality/latency/buffer effects. If design uses external selection, explicitly agree its transition behavior |
| A11 | Independent offline PC decoder, original image/frame format; §§2,3.4,6.3,7 | Host and Jetson decoding reported; designated PC release evidence incomplete | Partial | Designated Linux/Windows PC build(s), known Jetson streams decoded offline, agreed pixel layout preserved, exact/hash/quality checks |
| A12 | Self-contained C/C++ and dependency/offline constraints; §§1,3.1,4,7 | C++17 Release binaries built; standard-library implementation claimed | Partial pending audit | Clean build, source/license/dependency inventory, linked-runtime inspection and disconnected execution; exclude optional external GStreamer from compliant configuration |
| A13 | Continuous image/frame-stream operation; §§2,3.1,5 | Offline sequences and GOP1; frame counts/order validated | Partial | Agreed-duration stream, declared cadence, no unbounded queues, no hidden drops; resource and complete latency measurements. Temporal prediction is not mandated |
| A14 | Integrity and performance/resource evaluation; §§3.4,5,6.5–6.6 | CRC/concealment and stored-loss host report; encode/decode metrics | Partial | Defined integrity/error cases, actual received-data validation, reproducible CPU/GPU/memory measurements. No invented resource cap or required GPU use |
| A15 | Complete software, specifications, API, integration and performance handover; §§6.1–6.7,7 | Source, CMake, CLI, format/network reports, current documentation | Partial | Tagged source, encoder/decoder executables and hashes, clean build instructions, all seven deliverable groups and reproducible evidence reviewed |

## Handover checklist: every SoW §6 deliverable

| Section | Deliverable | Required completion record |
|---|---|---|
| 6.1 | Compression algorithm design | Architecture, compression method, rate-control/bandwidth mechanism, latency analysis, quality considerations |
| 6.2 | Encoder software | Complete C/C++ source, build scripts, configuration and designated Jetson executable |
| 6.3 | Decoder software | Complete source/builds, designated Linux and/or Windows executable(s), parsing and reconstruction interface; resolve broader §2 wording |
| 6.4 | Bitstream/metadata specification | Frame, packet, metadata, file and Ethernet interface definitions, versioning and validation |
| 6.5 | Test/validation suite | Compression/decompression, rates, varying bandwidth, VIS/IR, latency and integrity tests |
| 6.6 | Performance report | Rate/compression ratio, quality, encoder/decoder latency, resources and bandwidth behavior |
| 6.7 | Documentation | Design document, API reference, bitstream specification and integration instructions |

## What is optional, and what is a genuine gap

| Item | Treatment |
|---|---|
| 4/8 Mbps | Authorized engineering tests; primary acceptance rate remains 2 Mbps pending agreed final envelope |
| ROI | Desirable in clarifications, not an automatic mandatory gate |
| Temporal prediction, two-layer architecture, GPU acceleration | Implementation choices, not automatically required by S1 |
| Every IR spectral band or multispectral multiplexing | Not implied; single stream is the current clarification |
| Specific sequence count, 30 fps, fixed soak duration or loss percentages | Engineering test settings requiring agreement; not fabricated SoW thresholds |
| Colour, depth range, metadata, transport, offline/dependency and delivery obligations | Full-scope requirements retained; a week's priority decision cannot silently move them out of MVP |

**Acceptance rule:** all applicable mandatory gates pass for the agreed envelope, all scope conflicts/deviations are written down and approved, and complete reproducible deliverables are handed over. Correctness checks passing does not hide deadline misses or quality gaps.
