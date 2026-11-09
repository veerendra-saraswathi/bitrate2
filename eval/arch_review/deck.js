// ewc runs, experiments and final architectures — follows the style of
// "Combined Architectures A · D Comparison" (navy/tan, Georgia titles, Calibri text).
const pptxgen = require('pptxgenjs');
const fs = require('fs');
const { applyTheme } = require(process.env.SKILL + '/scripts/apply_theme.js');

const THEME = {
  name: 'ewc Navy Tan',
  headFontFace: 'Georgia',
  bodyFontFace: 'Calibri',
  colors: {
    dk1: '1A202C', lt1: 'FFFFFF', dk2: '0B2344', lt2: 'F4F1EA',
    accent1: '0B2344', accent2: 'B5935A', accent3: '4A5568', accent4: 'D4AF70',
    accent5: '718096', accent6: '2F6B4F', hlink: '0B2344', folHlink: '718096',
  },
};
const HEX = { navy: '0B2344', tan: 'B5935A', gold: 'D4AF70', ink: '1A202C', slate: '4A5568', mute: '718096', rule: 'E2E8F0', card: 'F4F1EA', green: '2F6B4F', red: '9B2C2C' };

const pres = new pptxgen();
pres.layout = 'LAYOUT_16x9';                  // 10 x 5.625 in, as the template
pres.title = 'ewc — Runs, Experiments and Final Architectures';
pres.author = 'RCI SoW Problem 4 team';
pres.theme = { headFontFace: THEME.headFontFace, bodyFontFace: THEME.bodyFontFace };
const C = pres.SchemeColor;
const SCORES = JSON.parse(fs.readFileSync('scores.json'));
const TOTAL = 36;

// ---------- layouts
pres.defineSlideMaster({
  title: 'TITLE_DARK', background: { color: C.text2 },
  objects: [
    { rect: { x: 0, y: 0, w: 10, h: 0.08, fill: { color: C.accent2 } } },
    { placeholder: { options: { name: 'kicker', type: 'body', x: 0.5, y: 1.4, w: 9, h: 0.35, fontSize: 11, color: C.accent4, bold: true, charSpacing: 3 }, text: '' } },
    { placeholder: { options: { name: 'title', type: 'title', x: 0.5, y: 1.85, w: 9, h: 0.9, fontSize: 32, bold: true, color: C.background1, fontFace: 'Georgia', align: 'left' }, text: '' } },
    { placeholder: { options: { name: 'body', type: 'body', x: 0.5, y: 2.85, w: 8.5, h: 0.9, fontSize: 16, color: C.accent4 }, text: '' } },
    { placeholder: { options: { name: 'foot', type: 'body', x: 0.5, y: 4.55, w: 9, h: 0.3, fontSize: 11, color: C.accent5 }, text: '' } },
  ],
});
pres.defineSlideMaster({
  title: 'DIVIDER', background: { color: C.text2 },
  objects: [
    { rect: { x: 0, y: 0, w: 10, h: 0.08, fill: { color: C.accent2 } } },
    { placeholder: { options: { name: 'title', type: 'title', x: 0.5, y: 2.0, w: 9, h: 0.75, fontSize: 32, bold: true, color: C.background1, fontFace: 'Georgia', align: 'left' }, text: '' } },
    { placeholder: { options: { name: 'body', type: 'body', x: 0.5, y: 2.8, w: 9, h: 0.5, fontSize: 16, color: C.accent4 }, text: '' } },
  ],
});
pres.defineSlideMaster({
  title: 'CONTENT', background: { color: C.background1 },
  objects: [
    { placeholder: { options: { name: 'kicker', type: 'body', x: 0.35, y: 0.18, w: 9.3, h: 0.3, fontSize: 8, color: C.accent2, bold: true, charSpacing: 3 }, text: '' } },
    { placeholder: { options: { name: 'title', type: 'title', x: 0.35, y: 0.5, w: 9.3, h: 0.55, fontSize: 22, bold: true, color: C.text2, fontFace: 'Georgia', align: 'left' }, text: '' } },
    { placeholder: { options: { name: 'sub', type: 'body', x: 0.35, y: 1.05, w: 9.3, h: 0.33, fontSize: 11, color: C.accent5 }, text: '' } },
    { rect: { x: 0, y: 5.25, w: 10, h: 0.375, fill: { color: C.text2 } } },
    { text: { text: 'ewc Runs & Trials  ·  RCI SoW Problem 4', options: { x: 0.3, y: 5.25, w: 7, h: 0.375, fontSize: 9, color: C.accent5, valign: 'middle', margin: 0 } } },
  ],
  slideNumber: { x: 8.9, y: 5.3, w: 0.8, h: 0.28, fontSize: 9, color: C.accent5, align: 'right' },
});

// ---------- helpers
let sectionName = '';
function section(t) { sectionName = t; pres.addSection({ title: t }); }
function titleSlide(kicker, title, body, foot) {
  const s = pres.addSlide({ masterName: 'TITLE_DARK', sectionTitle: sectionName });
  s.addText(kicker, { placeholder: 'kicker' }); s.addText(title, { placeholder: 'title' });
  s.addText(body, { placeholder: 'body' }); s.addText(foot, { placeholder: 'foot' }); return s;
}
function divider(title, body) {
  const s = pres.addSlide({ masterName: 'DIVIDER', sectionTitle: sectionName });
  s.addText(title, { placeholder: 'title' }); s.addText(body, { placeholder: 'body' }); return s;
}
function content(kicker, title, sub) {
  const s = pres.addSlide({ masterName: 'CONTENT', sectionTitle: sectionName });
  s.addText(kicker.toUpperCase(), { placeholder: 'kicker' }); s.addText(title, { placeholder: 'title' });
  if (sub) s.addText(sub, { placeholder: 'sub' });
  return s;
}
// table: rows[0] = header; hl = set of "r,c" cells to emphasise; colW optional
function table(s, rows, opt = {}) {
  const fs = opt.fontSize || 10;
  const body = rows.map((r, ri) => r.map((cell, ci) => {
    const isH = ri === 0, em = opt.hl && opt.hl.has(`${ri},${ci}`);
    return { text: String(cell), options: {
      bold: isH || em || (opt.boldFirstCol && ci === 0), color: isH ? HEX.slate : em ? HEX.navy : HEX.ink,
      fill: { color: em ? 'EFE6D2' : 'FFFFFF' }, fontSize: isH ? fs - 0.5 : fs, align: ci === 0 && opt.leftFirst !== false ? 'left' : (opt.align || 'center'),
      valign: 'middle', margin: [2, 4, 2, 4],
      border: [{ type: 'none' }, { type: 'none' }, { pt: isH ? 1 : 0.75, color: isH ? HEX.tan : HEX.rule }, { type: 'none' }],
    } };
  }));
  s.addTable(body, { x: opt.x || 0.35, y: opt.y || 1.5, w: opt.w || 9.3, colW: opt.colW, rowH: opt.rowH || 0.3, fontFace: 'Calibri', objectName: opt.name || 'table' });
}
function note(s, text, y = 4.72) {
  s.addText(text, { x: 0.4, y, w: 9.2, h: 0.4, fontSize: 10.5, italic: true, color: HEX.slate, isTextBox: true, margin: 0, valign: 'top', objectName: 'takeaway' });
}
function bullets(s, items, opt = {}) {
  s.addText(items.map((t, i) => ({ text: t, options: { bullet: { indent: 14 }, breakLine: i < items.length - 1 } })), {
    x: opt.x || 0.5, y: opt.y || 1.6, w: opt.w || 9, h: opt.h || 3.0, fontSize: opt.fontSize || 14, color: HEX.ink,
    paraSpaceAfter: 6, valign: 'top', isTextBox: true, objectName: opt.name || 'bullets',
  });
}
function cards(s, list, opt = {}) {      // [{h, t}] stacked cards like the template "Theoretical Basis"
  const y0 = opt.y || 1.55, h = opt.h || 0.85, gap = 0.15, w = opt.w || 9.3, x = opt.x || 0.35;
  list.forEach((c, i) => {
    const y = y0 + i * (h + gap);
    s.addShape(pres.shapes.RECTANGLE, { x, y, w, h, fill: { color: HEX.card }, line: { color: HEX.rule, width: 0.75 }, objectName: `card${i + 1}` });
    s.addText([{ text: c.h, options: { bold: true, color: HEX.navy, fontSize: 12.5, breakLine: true } }, { text: c.t, options: { color: HEX.slate, fontSize: 11 } }],
      { x: x + 0.15, y: y + 0.06, w: w - 0.3, h: h - 0.12, valign: 'middle', margin: 0, isTextBox: true, objectName: `card${i + 1}_text` });
  });
}
function stats(s, list, y = 1.6) {        // big-number callouts
  const n = list.length, w = (9.3 - 0.3 * (n - 1)) / n;
  list.forEach((st, i) => {
    const x = 0.35 + i * (w + 0.3);
    s.addShape(pres.shapes.RECTANGLE, { x, y, w, h: 1.55, fill: { color: HEX.card }, line: { color: HEX.rule, width: 0.75 }, objectName: `stat${i + 1}` });
    s.addText(st.v, { x: x + 0.15, y: y + 0.12, w: w - 0.3, h: 0.7, fontSize: 24, bold: true, color: HEX.navy, fontFace: 'Georgia', margin: 0, isTextBox: true, objectName: `stat${i + 1}_value` });
    s.addText(st.l, { x: x + 0.15, y: y + 0.85, w: w - 0.3, h: 0.62, fontSize: 11, color: HEX.slate, margin: 0, valign: 'top', isTextBox: true, objectName: `stat${i + 1}_label` });
  });
}
function twoCol(s, left, right, y = 1.6) {
  [left, right].forEach((col, i) => {
    const x = 0.35 + i * 4.8;
    s.addShape(pres.shapes.RECTANGLE, { x, y, w: 4.5, h: 2.95, fill: { color: i ? 'FFFFFF' : HEX.card }, line: { color: HEX.rule, width: 0.75 }, objectName: `col${i + 1}` });
    s.addText([{ text: col.h, options: { bold: true, color: HEX.navy, fontSize: 13, breakLine: true } },
      ...col.items.map((t, k) => ({ text: t, options: { bullet: { indent: 12 }, fontSize: 12, color: HEX.ink, breakLine: k < col.items.length - 1 } }))],
      { x: x + 0.15, y: y + 0.1, w: 4.2, h: 2.75, valign: 'top', paraSpaceAfter: 4, margin: 0, isTextBox: true, objectName: `col${i + 1}_text` });
  });
}
function stage(s, rows) {                 // POC · MVP · Final rows
  rows.forEach((r, i) => {
    const y = 1.6 + i * 1.0;
    s.addShape(pres.shapes.OVAL, { x: 0.45, y: y + 0.1, w: 0.6, h: 0.6, fill: { color: HEX.navy }, line: { color: HEX.navy }, objectName: `stage${i + 1}_dot` });
    s.addText(r.k, { x: 0.45, y: y + 0.1, w: 0.6, h: 0.6, align: 'center', valign: 'middle', fontSize: 10, bold: true, color: 'FFFFFF', margin: 0, isTextBox: true, objectName: `stage${i + 1}_key` });
    s.addText([{ text: r.h + ' — ', options: { bold: true, color: HEX.navy } }, { text: r.t, options: { color: HEX.ink } }],
      { x: 1.25, y, w: 8.3, h: 0.8, fontSize: 13.5, valign: 'middle', margin: 0, isTextBox: true, objectName: `stage${i + 1}_text` });
  });
}
const chartBase = (title) => ({
  showTitle: true, title, titleFontSize: 12, titleColor: HEX.navy, titleFontFace: '+mn-lt',
  catAxisLabelColor: HEX.slate, valAxisLabelColor: HEX.slate, catAxisLabelFontSize: 10, valAxisLabelFontSize: 9,
  catAxisLabelFontFace: '+mn-lt', valAxisLabelFontFace: '+mn-lt', dataLabelFontFace: '+mn-lt',
  valGridLine: { color: HEX.rule, size: 0.5 }, catGridLine: { style: 'none' },
  showValue: true, dataLabelFontSize: 9, dataLabelColor: HEX.ink, legendFontSize: 10, legendFontFace: '+mn-lt',
});

// =============================================================== 1. Opening
section('Opening');
titleSlide('RCI SoW PROBLEM 4', 'ewc: Runs, Trials & Scores',
  'Every run and trial from this project, with measured scores\nA and D re-measured  ·  ewc wavelet  ·  D+  ·  Two-layer',
  'Low-Bitrate, High-Resolution Image/Video Compression with Low Latency  ·  6 Nov 2026');

let s = content('Summary', 'What Changed, in Four Numbers', 'All measured on the same FLIR ADAS v2 14-bit frames unless noted');
stats(s, [
  { v: '41.2 dB', l: 'ewc wavelet at 2 Mbps (PSNR, scene range). D (CharLS): 33.4 dB. A: 6.4 dB.' },
  { v: '−9.1%', l: 'D+ file size vs CharLS (median of 63 points). Smaller on 62 of 63.' },
  { v: '≤ 20 DN', l: 'Guaranteed worst-pixel error at 2 Mbps on video (two-layer mode).' },
  { v: '0', l: 'Crashes or hangs in 360 corrupted-stream decodes (release build).' },
]);
note(s, 'One codec (ewc), one file format, one decoder: wavelet for 2 Mbps, D+ for lossless / near-lossless, two-layer for 2 Mbps with a bound.', 3.45);
s.addNotes('Headline: the wavelet path wins the SoW 2 Mbps target by a wide margin; D+ now beats CharLS for lossless and near-lossless; the two-layer mode adds a guaranteed worst-pixel error.');

s = content('Method', 'How Every Test Was Run', 'Same data, same metric code, same budget for all architectures');
table(s, [
  ['Item', 'Setting'],
  ['14-bit data', 'FLIR ADAS v2: 6 video clips + 1 still set, 640×512, 14-bit'],
  ['8-bit data', 'KAIST thermal and visible 640×512 frames (bitrate-main contract frames)'],
  ['Target rate', '2 Mbps at 30 fps = 8,333 bytes per frame, never exceeded'],
  ['Quality metrics', 'PSNR (14-bit peak and scene range), SSIM, MAE, RMSE, max error'],
  ['Speed', 'Encode / decode median ms, 2 threads, idle machine; ≤ 2 frames = 66.7 ms'],
  ['Robustness', 'Bit flips, deleted / zeroed packets, bursts in the stored file; fuzzing'],
  ['Fairness to A and D', 'Best-case search per frame for their QP / NEAR to fit the budget'],
], { colW: [2.2, 7.1], boldFirstCol: true, align: 'left', rowH: 0.36, fontSize: 11 });
note(s, 'Every number in this deck is in the repo: eval/arch_review/ and eval/results/exp/.', 4.6);

// =============================================================== 2. Starting point
section('Starting Point: A and D');
divider('Starting Point', 'Architectures A and D — re-measured on our data');

s = content('Architecture A', 'A, Re-measured at the SoW Target', 'bitrate-main 5/3 wavelet  ·  exhaustive QP and level search per frame (best case for A)');
table(s, [
  ['Test', 'Bytes', 'PSNR (dB)', 'SSIM', 'Max error', 'Note'],
  ['8-bit thermal, 2 Mbps', '5,808', '18.8', '0.16', '45', 'Next QP step jumps to 10.3 KB'],
  ['8-bit visible, 2 Mbps', '8,116', '9.8', '0.03', '159', 'Near-blank frame (5 grey levels)'],
  ['14-bit FLIR, as shipped', '8,284', '−22.9', '0.008', '11,070', 'Keeps only the top 8 bits'],
  ['14-bit FLIR, contrast-stretched', '7,432', '6.4', '0.023', '2,506', 'Free side information given to A'],
  ['8-bit thermal, lossless', '247,741', 'exact', '1.0', '0', '6.8× larger than D+'],
], { colW: [2.4, 0.9, 1.0, 0.8, 1.0, 3.2], rowH: 0.36, fontSize: 10.5 });
note(s, 'A cannot reach 2 Mbps with usable quality, has no rate control and no 14-bit path. STATUS.md’s ~20.3 Mbps lossless did not reproduce (59.5 Mbps measured).', 4.3);

s = content('Architecture D', 'D, Re-measured — Published Points Reproduced', 'JPEG-LS via CharLS  ·  same library bitrate-main used');
table(s, [
  ['Point', 'Bytes', 'Quality', 'Max error', 'Matches STATUS.md'],
  ['8-bit thermal NEAR=0', '41,067', 'bit-exact', '0', 'Yes, byte-exact'],
  ['8-bit thermal NEAR=1', '22,794', '50.32 dB', '1', 'Yes, byte-exact'],
  ['8-bit thermal NEAR=4', '8,656', '41.0 dB', '4', 'Yes (2.08 Mbps)'],
  ['8-bit thermal at 2 Mbps', '6,887', '39.8 dB', '5', 'NEAR=4 is 4% over budget'],
  ['8-bit visible at 2 Mbps', '7,857', '25.2 dB', '31', 'Lowest published point is 12.6 Mbps'],
  ['14-bit FLIR at 2 Mbps (7 clips)', '8,030', '33.4 dB, SSIM 0.72', '140', 'New measurement'],
], { colW: [2.6, 1.0, 1.9, 1.0, 2.8], rowH: 0.36, fontSize: 10.5 });
note(s, 'D’s published numbers are real. They look strong because most sit at 3–16× the 2 Mbps budget.', 4.3);

// =============================================================== 3. ewc wavelet
section('ewc Wavelet');
divider('ewc Wavelet', '2 Mbps main path — tiled 9/7 wavelet with exact rate control');

s = content('ewc wavelet', 'What Is the ewc Wavelet Path?', 'Self-contained C++17, no external libraries');
bullets(s, [
  'Subtract the frame mean; scale samples by 2^(18 − bit depth) so the integer transform loses nothing',
  'CDF 9/7 wavelet in 128×128 tiles (PoC) or whole frame; band weights for perceived error',
  'Bit-plane coding (SPIHT trees) with an adaptive arithmetic coder — any prefix is a valid image',
  'Rate control cuts every tile at the best point for exactly 8,333 bytes — never over budget',
  'CRC-32 header, CRC-16 per 256 bytes, redundant copy of key bytes; damaged tiles are concealed',
]);

s = content('ewc wavelet', 'Theoretical Basis', '9/7 wavelet · embedded coding · rate-distortion cut');
cards(s, [
  { h: 'Cohen, Daubechies & Feauveau, 1992', t: 'Biorthogonal bases of compactly supported wavelets (the 9/7 filter, JPEG 2000 lossy)' },
  { h: 'Said & Pearlman, 1996', t: 'SPIHT: set partitioning in hierarchical trees — embedded bit-plane coding' },
  { h: 'Taubman, 2000 (EBCOT)', t: 'Post-compression rate-distortion optimal truncation, as used for the per-tile cut' },
]);

s = content('ewc wavelet', 'Measured Results at 2 Mbps', '14-bit FLIR, 7 clips × 10 frames, 8,333 B per frame');
table(s, [
  ['Variant', 'PSNR scene (dB)', 'PSNR 14-bit (dB)', 'SSIM', 'MAE (DN)', 'Max bytes', 'Encode / decode'],
  ['Whole frame', '41.24', '71.49', '0.902', '3.55', '8,332', '8.9 / 4.7 ms'],
  ['Tiled 128×128 (PoC default)', '41.19', '71.44', '0.902', '3.57', '8,328', '6.3 / 3.2 ms'],
], { colW: [2.4, 1.2, 1.2, 0.8, 1.0, 1.1, 1.6], rowH: 0.42, fontSize: 11, hl: new Set(['2,0']) });
stats(s, [{ v: '0.33', l: 'of a frame time: worst tiled encode in 1,000 frames (10.9 ms of 33.3 ms)' }, { v: '−0.05 dB', l: 'cost of tiling vs whole frame — paid for damage isolation and parallel tiles' }], 3.0);

s = content('ewc wavelet', 'Role by Stage', 'POC · MVP · Final');
stage(s, [
  { k: 'POC', h: 'POC', t: 'Strong. Meets 2 Mbps, 14-bit, latency and robustness on FLIR data.' },
  { k: 'MVP', h: 'MVP', t: 'Primary 2 Mbps path. Needs Jetson measurement and a C API wrapper.' },
  { k: 'FIN', h: 'Final', t: 'Primary lossy path; tiles allow early start once allocation is made per row.' },
]);

// =============================================================== 4. Experiments
section('Experiments');
divider('Experiments', 'Hypothesis → implement → test → benchmark → keep or reject');

s = content('Experiments', 'All Trials at a Glance', 'Each one committed separately on branch exp-2026-11-05');
table(s, [
  ['#', 'Trial', 'Measured result', 'Decision'],
  ['1', 'Persistent thread pool', 'Encode max 14.4–20.5 → 10.9–11.7 ms; p99 −15%', 'Kept'],
  ['2', 'Compact tile index', '64×64 tiles +0.05 dB (all 7 clips); 128×128 +0.007 dB', 'Kept'],
  ['3', '--poc --lossless fix', 'Was failing; now bit-exact, with regression test', 'Kept'],
  ['4', 'Precision shift (wavelet bug)', '+0.41 dB on 14-bit (all clips); +8.5 dB on 8-bit thermal', 'Kept'],
  ['5', 'D+ (arithmetic-coded JPEG-LS)', 'Smaller than CharLS on 62 / 63 points, median −9.1%', 'Kept'],
  ['6', 'Two-layer bounded error', 'Worst pixel ≤ 10–20 DN at 2 Mbps on video, −1.5 dB', 'Kept (optional)'],
  ['7', 'HTJ2K replaces coder', '+0.07 to +0.40 dB in robust framing, −0.90 on stills', 'Rejected'],
  ['8', 'GStreamer transport', 'No compression; FEC needs 20–50 ms extra buffer', 'Wrapper only'],
  ['9', 'Temporal (P-frames)', '+0.3 dB for 4× encode time and error spread', 'Rejected'],
], { colW: [0.4, 2.6, 4.8, 1.5], rowH: 0.33, fontSize: 10, align: 'left', leftFirst: true });

s = content('Experiment 4', 'Precision Shift — the Wavelet Bug', 'Integer 9/7 lifting rounded at every step: a ~1 DN noise floor');
s.addChart(pres.charts.BAR, [
  { name: 'Before', labels: ['4FRnNp', '5RSrbW', '6tLtjd', 'SCiKdG', 'ePoikf', 'vbrSzr', 'Stills'], values: [40.12, 34.0, 41.44, 42.24, 40.29, 40.87, 42.53] },
  { name: 'After', labels: ['4FRnNp', '5RSrbW', '6tLtjd', 'SCiKdG', 'ePoikf', 'vbrSzr', 'Stills'], values: [40.51, 34.59, 41.93, 42.57, 40.76, 41.28, 42.7] },
], { x: 0.35, y: 1.45, w: 5.9, h: 3.65, barDir: 'col', barGapWidthPct: 60, chartColors: ['A0AEC0', HEX.navy],
  ...chartBase('PSNR (scene range, dB) at 2 Mbps, 30 frames per clip'), valAxisMinVal: 30, valAxisMaxVal: 45, showLegend: true, legendPos: 'b',
  dataLabelFormatCode: '0.0', dataLabelFontSize: 7, objectName: 'pshift_chart' });
table(s, [
  ['Data at 2 Mbps', 'Before', 'After'],
  ['14-bit FLIR mean', '40.21 dB', '40.62 dB'],
  ['8-bit thermal', '43.3 dB, e9', '51.9 dB, e4'],
  ['8-bit thermal 5.5 Mbps', '44.6 dB', '56.0 dB'],
  ['Encode time', '—', 'unchanged'],
], { x: 6.45, y: 1.6, w: 3.2, colW: [1.4, 0.9, 0.9], rowH: 0.42, fontSize: 10, hl: new Set(['1,2', '2,2', '3,2']) });

s = content('Experiment 7', 'HTJ2K (OpenJPH) as the Coder', 'JPEG 2000 Part 15 — a standards-based alternative');
table(s, [
  ['Test', 'Result vs ewc'],
  ['Whole frame, same bytes', '+0.45 dB, encode 3× faster, decode 2× faster'],
  ['With realistic rate control (1.0–1.3 encodes/frame)', '+0.31 to +0.57 dB on video, never over budget'],
  ['Bare codestream with bit errors', '64% rejected, 35% decoded silently wrong, 0 crashes'],
  ['Inside ewc tiled, error-protected framing', '+0.07 to +0.40 dB on video, −0.90 dB on stills'],
  ['Dependency', 'External library (OpenJPH, BSD) — not allowed in the final codec'],
], { colW: [4.0, 5.3], rowH: 0.42, fontSize: 11, align: 'left' });
note(s, 'Rejected: the gain disappears once robustness is added, and silent errors are unsafe. Kept as a next-phase idea if per-block resync is added.', 4.3);

s = content('Experiment 8', 'GStreamer Transport at 5% Packet Loss', '150 frames, loopback UDP, clip ePoikf');
table(s, [
  ['Setup', 'Frames intact / 150', 'Added latency (median / p99)'],
  ['GStreamer, no FEC', '114', '0.4 ms'],
  ['GStreamer ULPFEC 25%, no jitter buffer', '137–140', '0.5 / 34 ms'],
  ['GStreamer ULPFEC 25%, 20 ms buffer', '146', '20.6 / 54 ms'],
  ['GStreamer ULPFEC 50%, 20 ms buffer', '149', '20.6 / 54 ms'],
  ['ewc native, no FEC', '105 + 35 concealed', '—'],
  ['ewc native RS k=4 (25% overhead)', '149', 'no extra buffering'],
  ['ewc native RS k=2', '150', 'no extra buffering'],
], { colW: [4.0, 2.4, 2.9], rowH: 0.33, fontSize: 10.5, hl: new Set(['6,1', '7,1']) });
note(s, 'GStreamer adds transport, not compression. ewc’s own FEC recovers more with no added delay.', 4.75);

s = content('Experiment 9', 'Temporal Prediction (P-frames) at 2 Mbps', '6 video clips × 60 frames');
s.addChart(pres.charts.BAR, [{ name: 'PSNR', labels: ['Intra (PoC)', 'GOP 4', 'GOP 8', 'GOP 16', 'GOP 30'], values: [39.82, 40.11, 40.13, 40.13, 40.12] }],
  { x: 0.35, y: 1.45, w: 4.6, h: 3.4, barDir: 'col', chartColors: [HEX.navy], ...chartBase('PSNR (scene range, dB)'), showLegend: false,
    valAxisMinVal: 38, valAxisMaxVal: 41, dataLabelFormatCode: '0.00', objectName: 'gop_psnr' });
s.addChart(pres.charts.BAR, [{ name: 'Encode ms', labels: ['Intra (PoC)', 'GOP 4', 'GOP 8', 'GOP 16', 'GOP 30'], values: [6.7, 24.7, 25.0, 25.0, 26.5] }],
  { x: 5.1, y: 1.45, w: 4.55, h: 3.4, barDir: 'col', chartColors: [HEX.tan], ...chartBase('Encode time per frame (ms)'), showLegend: false,
    valAxisMinVal: 0, dataLabelFormatCode: '0.0', objectName: 'gop_time' });
note(s, 'Rejected: +0.3 dB costs 4× encode time and spreads errors across frames. FLIR frames differ as much as neighbouring pixels do.', 4.9);

// =============================================================== 5. D+
section('D+');
divider('D+', 'Architecture D improved with ewc’s arithmetic coder');

s = content('D+', 'What Is D+?', 'JPEG-LS method, ewc entropy coding, ewc error protection');
twoCol(s, { h: 'Kept from D (JPEG-LS)', items: ['Predict each pixel from its neighbours', '365 gradient contexts with bias correction', 'Run mode for flat areas', 'Exact max error: ±NEAR (NEAR=0 is bit-exact)'] },
  { h: 'Added from ewc', items: ['Adaptive arithmetic coder instead of Golomb codes — can go below 1 bit per pixel', 'Automatic stripe height: taller stripes when a stripe would be under 3 KB', 'CRC-protected independent stripes: damage stays local', 'Pure C++, no CharLS'] });

s = content('D+', 'Theoretical Basis', 'Context modelling · arithmetic coding');
cards(s, [
  { h: 'Weinberger, Seroussi & Sapiro, 2000', t: 'LOCO-I / JPEG-LS (ISO/IEC 14495-1): prediction, contexts, bias cancellation' },
  { h: 'Witten, Neal & Cleary, 1987; Martin, 1979', t: 'Arithmetic coding and range coding — code below 1 bit per symbol' },
  { h: 'ISO/IEC 14495-2 (JPEG-LS Part 2)', t: 'Arithmetic-coding extension of JPEG-LS: the same idea, standardised' },
]);

s = content('D+', 'Measured Results — File Size vs CharLS', '14-bit FLIR 7 clips × 5 frames; bound checked on every frame');
s.addChart(pres.charts.BAR, [{ name: 'D+ vs CharLS', labels: ['NEAR 0', 'NEAR 1', 'NEAR 2', 'NEAR 4', 'NEAR 8', 'NEAR 16', 'NEAR 32'], values: [-1.4, -2.2, -2.5, -7.1, -10.6, -12.2, -13.2] }],
  { x: 0.35, y: 1.45, w: 4.9, h: 3.15, barDir: 'col', chartColors: [HEX.green], ...chartBase('Size change vs CharLS (%), 14-bit FLIR'), showLegend: false,
    dataLabelFormatCode: '0.0', valAxisMaxVal: 0, valAxisMinVal: -15, objectName: 'dplus_chart' });
table(s, [
  ['NEAR', 'CharLS', 'ewc Golomb', 'D+'],
  ['0', '185,273', '187,941', '182,756'],
  ['1', '120,804', '122,989', '118,104'],
  ['4', '65,084', '66,355', '60,476'],
  ['16', '12,784', '13,963', '11,228'],
  ['32', '6,090', '7,358', '5,286'],
], { x: 5.45, y: 1.6, w: 4.2, colW: [0.7, 1.15, 1.2, 1.15], rowH: 0.4, fontSize: 10, hl: new Set(['1,3', '2,3', '3,3', '4,3', '5,3']) });
note(s, 'Bytes per frame. 8-bit: thermal −11.5% to −27.3%, visible −0.5% to −15.0% (lossless +0.1%).', 4.75);

s = content('D+', 'Implementation Status', 'In src/loco.cpp; default for --lossless');
twoCol(s, { h: 'Completed', items: ['Encoder and decoder, both back-ends readable', 'Bound verified on every test frame', 'Robustness equal or better than Golomb under stored losses', 'Decoder hang on corrupt input found and fixed; 360-case fuzz clean', 'ASan / UBSan / TSan clean'] },
  { h: 'Open', items: ['Speed: 14-bit lossless 23 ms vs 13 ms (Golomb) vs 7 ms (CharLS, 1 thread)', 'Old decoders cannot read new files (use --golomb for them)', 'No Jetson measurement yet', 'Native fixed-rate control (today: NEAR search)'] });

// =============================================================== 6. Two-layer
section('Two-Layer');
divider('Two-Layer Mode', '2 Mbps with a guaranteed worst-pixel error');

s = content('Two-layer', 'How It Works', 'Layer 1 = wavelet at part of the budget  ·  Layer 2 = D+ on what is left');
table(s, [
  ['Step', 'What happens'],
  ['1', 'Code the frame with the ewc wavelet at part of the 8,333-byte budget'],
  ['2', 'Decode it exactly as the receiver will, and take the difference to the original'],
  ['3', 'Code the difference with D+ at max error k (centred in a 12-bit range)'],
  ['4', 'Try a few budget splits and “no layer 1”; keep the smallest that fits'],
  ['Result', 'No pixel is ever more than k off. If layer 2 is lost, layer 1 still decodes'],
], { colW: [1.0, 8.3], rowH: 0.42, fontSize: 11.5, align: 'left' });
note(s, '“Guaranteed max error” caps the worst pixel. It does not mean less loss on average — average quality drops a little.', 4.35);

s = content('Two-layer', 'Measured Results at 2 Mbps', 'Per clip, 10 frames; wavelet-only shown for comparison');
table(s, [
  ['Clip', 'Wavelet PSNR', 'Wavelet worst pixel', 'Two-layer PSNR', 'Two-layer bound', 'Bytes', 'Encode'],
  ['4FRnNp', '40.52', '24', '39.28', '≤ 14', '6,301', '39 ms'],
  ['5RSrbW', '34.58', '19', '33.77', '≤ 10', '5,991', '49 ms'],
  ['6tLtjd', '41.93', '35', '40.45', '≤ 14', '7,505', '53 ms'],
  ['SCiKdG', '47.44', '39', '45.30', '≤ 20', '7,179', '47 ms'],
  ['ePoikf', '40.69', '27', '39.00', '≤ 14', '7,131', '55 ms'],
  ['vbrSzr', '40.73', '24', '39.37', '≤ 14', '5,938', '50 ms'],
  ['Stills', '42.43', '130', 'does not fit', '—', '—', '—'],
], { colW: [1.2, 1.3, 1.5, 1.4, 1.4, 1.1, 1.4], rowH: 0.36, fontSize: 10.5, hl: new Set(['1,4', '2,4', '3,4', '4,4', '5,4', '6,4']) });
note(s, 'Worst pixel roughly halved and guaranteed, for −0.8 to −2.1 dB. Encode 39–55 ms: inside 2 frames, not 1.', 4.7);

// =============================================================== 7. Full comparison
section('Full Comparison');
divider('Full Comparison', 'All architectures side by side');

s = content('Comparison', 'Architectures Evaluated', 'Core approach and status');
table(s, [
  ['Architecture', 'Approach', 'In final codec?', 'Status'],
  ['A', '5/3 wavelet + QP, 8-bit', 'No', 'Retired'],
  ['D (CharLS)', 'JPEG-LS, Golomb codes, external library', 'No', 'Replaced by D+'],
  ['ewc near-lossless (Golomb)', 'JPEG-LS method in-house', 'Decode only', 'Superseded'],
  ['D+', 'JPEG-LS method + arithmetic coder', 'Yes', 'Lossless / near-lossless path'],
  ['ewc wavelet, tiled', '9/7 wavelet + SPIHT + rate control', 'Yes', '2 Mbps main path'],
  ['Two-layer', 'Wavelet base + D+ residual', 'Yes (tool)', 'Optional bounded-error mode'],
  ['HTJ2K / P-frames / GStreamer', 'See experiments', 'No', 'Rejected / wrapper only'],
], { colW: [2.4, 3.3, 1.5, 2.1], rowH: 0.36, fontSize: 10.5 });

s = content('Comparison', 'The SoW Target: 2 Mbps, 14-bit', '7 FLIR clips × 10 frames  ·  8,333 B per frame  ·  same metric code');
table(s, [
  ['Architecture', 'PSNR scene', 'PSNR 14-bit', 'SSIM', 'MAE', 'Max err', 'Enc / dec ms'],
  ['A, as shipped', '−22.88', '7.37', '0.008', '6,922', '11,070', '2.2 / 1.8 + 5 s search'],
  ['A, contrast-stretched', '6.41', '36.66', '0.023', '272', '2,506', '1.9 / 1.4 + 5 s search'],
  ['D (CharLS)', '33.38', '63.63', '0.724', '11.57', '140', '0.8 / 0.7 (1 thread)'],
  ['ewc near-lossless (Golomb)', '32.11', '62.36', '0.670', '13.59', '150', '1.4 / 1.2'],
  ['D+', '34.35', '64.60', '0.752', '9.79', '107', '5.2 / 1.7'],
  ['ewc wavelet, whole frame', '41.24', '71.49', '0.902', '3.55', '155', '8.9 / 4.7'],
  ['ewc wavelet, tiled (PoC)', '41.19', '71.44', '0.902', '3.57', '130', '6.3 / 3.2'],
], { colW: [2.3, 1.0, 1.0, 0.8, 0.9, 0.9, 2.4], rowH: 0.34, fontSize: 10, hl: new Set(['6,1', '6,3', '6,4', '7,1', '7,3']) });
note(s, 'MAE and max error in sensor units (DN). The wavelet leads by 6.8 dB over D+ and 7.8 dB over CharLS.', 4.75);

s = content('Comparison', 'Quality at 2 Mbps — at a Glance', 'PSNR (scene range, dB), mean of 7 FLIR clips');
s.addChart(pres.charts.BAR, [{ name: 'PSNR', labels: ['A (stretched)', 'ewc Golomb', 'D (CharLS)', 'D+', 'Two-layer*', 'ewc wavelet'], values: [6.41, 32.11, 33.38, 34.35, 39.53, 41.19] }],
  { x: 0.35, y: 1.4, w: 9.3, h: 3.4, barDir: 'bar', chartColors: [HEX.navy], ...chartBase('PSNR at 2 Mbps (dB)'), showLegend: false,
    valAxisMinVal: 0, valAxisMaxVal: 45, dataLabelFormatCode: '0.0', objectName: 'psnr_2mbps' });
note(s, '* Two-layer: 6 video clips only (stills do not fit); it trades 1.5 dB for a guaranteed worst-pixel error of ≤ 20 DN.', 4.85);

s = content('Comparison', 'Lossless and Near-Lossless', 'Size for a set max error  ·  smaller is better');
table(s, [
  ['Data, max error', 'A', 'D (CharLS)', 'ewc Golomb', 'D+', 'Two-layer'],
  ['14-bit FLIR, lossless', 'not possible', '185,273', '187,941', '182,756', '= D+'],
  ['14-bit FLIR, NEAR 4', '—', '65,084', '66,355', '60,476', '61,343**'],
  ['14-bit FLIR, NEAR 16', '—', '12,784', '13,963', '11,228', '6,322**'],
  ['8-bit thermal, lossless', '247,741', '41,067', '41,954', '36,326', '= D+'],
  ['8-bit thermal, NEAR 4', '—', '8,656', '9,058', '6,922', '4,248'],
  ['8-bit visible, NEAR 4', '—', '52,418', '53,212', '45,538', '45,046'],
], { colW: [2.4, 1.2, 1.3, 1.3, 1.3, 1.8], rowH: 0.36, fontSize: 10.5, hl: new Set(['1,4', '2,4', '4,4', '5,5', '6,5', '3,5']) });
note(s, 'Bytes per frame. ** single ePoikf frame from the same-max-error test. D+ beats CharLS on 62 of 63 points.', 4.6);

s = content('Comparison', 'Errors in the Stored File', 'What the decoder does with damage');
table(s, [
  ['Architecture', 'One flipped bit', '16 flipped bits', 'Crashes'],
  ['A', '35% rejected, 62% silently wrong', '90% rejected, 11% silently wrong', '0'],
  ['D (CharLS)', '99.5% of frames lost', '100% lost', '0'],
  ['ewc (all paths)', '99% kept and marked damaged, 0 silently wrong', '92% kept and marked, 0 silently wrong', '0 (360-case fuzz)'],
], { colW: [1.8, 3.2, 3.2, 1.1], rowH: 0.5, fontSize: 11, hl: new Set(['3,1', '3,2']) });
note(s, 'Under packet loss and bursts, D+ is as reliable as the older ewc coder or better. A silently wrong frame is the worst outcome: nothing warns the user.', 4.0);

s = content('Comparison', 'Latency and Key SoW Criteria', '30 fps: 1 frame = 33.3 ms, limit = 2 frames');
table(s, [
  ['Criterion', 'A', 'D', 'D+', 'ewc wavelet', 'Two-layer'],
  ['Encode at 2 Mbps (incl. search)', '~5 s', '~7 ms', '~40 ms', '6.3 ms', '51 ms'],
  ['Meets ≤ 2 frames', 'No', 'Yes', 'Yes', 'Yes', 'Yes'],
  ['Pure C/C++, no external library', 'Yes', 'No', 'Yes', 'Yes', 'Yes'],
  ['14-bit input', 'No', 'Yes', 'Yes', 'Yes', 'Yes'],
  ['Exact fixed bitrate', 'No', 'No (search)', 'No (search)', 'Yes', 'Yes'],
  ['Bit-exact option', 'Yes (8-bit)', 'Yes', 'Yes', 'No', 'Via D+'],
  ['Guaranteed max error', 'No', 'Yes', 'Yes', 'No', 'Yes'],
], { colW: [2.9, 1.1, 1.2, 1.2, 1.4, 1.5], rowH: 0.36, fontSize: 10.5 });
note(s, 'D+ search time = about 8 trial encodes to find NEAR; the wavelet hits the budget in one pass.', 4.75);

s = content('Comparison', 'Fit by Stage', 'POC · MVP · Final');
table(s, [
  ['Stage', 'A', 'D (CharLS)', 'D+', 'ewc wavelet', 'Two-layer'],
  ['POC', 'Not suitable', 'Partial (external)', 'Strong', 'Strong', 'Strong (video)'],
  ['MVP', 'Retired', 'Reference only', 'Lossless path', 'Main 2 Mbps path', 'Optional mode'],
  ['Final', 'Retired', 'Not allowed', 'Lossless path', 'Main lossy path', 'If a bound is required'],
], { colW: [1.1, 1.4, 1.6, 1.6, 1.8, 1.8], rowH: 0.55, fontSize: 11 });

s = content('Comparison', 'Strengths and Weaknesses');
table(s, [
  ['Architecture', 'Strengths', 'Weaknesses'],
  ['A', 'Simple, fast per encode', 'No 2 Mbps, no 14-bit, no rate control, silent errors'],
  ['D (CharLS)', 'Fast, exact max error', 'External library; 1 bit loses the frame; weak at 2 Mbps'],
  ['D+', 'Best lossless / near-lossless size; exact max error; robust', 'Slower (23 ms lossless); weak at 2 Mbps'],
  ['ewc wavelet', 'Best 2 Mbps quality by 6.8 dB; exact rate; fast; robust', 'No max-error guarantee; lossy only'],
  ['Two-layer', 'Guaranteed worst pixel at 2 Mbps', '−1.5 dB average; 51 ms; stills do not fit'],
], { colW: [1.6, 3.9, 3.8], rowH: 0.45, fontSize: 11, align: 'left' });

// scores
const sc = SCORES;
const order = Object.keys(sc);
s = content('Comparison', 'Comparison Scores', 'Rubric out of 100, computed from the measured results above');
table(s, [
  ['Architecture', 'Quality 2 Mbps /25', 'Lossless /15', 'Latency /10', 'Robust /15', 'SoW fit /15', 'Max err /10', 'Platform /10', 'Total'],
  ...order.map(k => [k, sc[k].quality, sc[k].lossless, sc[k].latency, sc[k].robust, sc[k].comply, sc[k].maxerr, sc[k].platform, sc[k].total]),
], { colW: [2.6, 1.0, 0.85, 0.85, 0.85, 0.85, 0.85, 0.85, 0.6], rowH: 0.36, fontSize: 9.5,
  hl: new Set([`${order.length},8`, `${order.length},0`]) });
note(s, 'Quality = 25 × (PSNR − 30) / (41.2 − 30). Lossless = 15 × D+ size / size. Platform: A and D per their Jetson baselines; ewc not yet on Jetson.', 4.55);

s = content('Comparison', 'Final Architecture', 'One codec, three paths, one decoder');
table(s, [
  ['What you need', 'Path', 'Command', 'Score'],
  ['2 Mbps, best image (SoW PoC)', 'ewc wavelet, tiled', 'ewc_enc --poc', `${sc['ewc wavelet, tiled (PoC)'].total}`],
  ['2 Mbps, worst pixel capped', 'Two-layer', 'ewc_layered … 8333 k', `${sc['Two-layer (wavelet + D+)'].total}`],
  ['Lossless (perfect reconstruction)', 'D+', 'ewc_enc --lossless', `${sc['D+ (ewc)'].total}`],
  ['Near-lossless, max error N', 'D+', 'ewc_enc --lossless --near N', `${sc['D+ (ewc)'].total}`],
  ['All of the above', 'Final ewc codec', 'one file format', `${sc['Final ewc codec (all three paths)'].total}`],
], { colW: [3.0, 2.0, 3.1, 1.2], rowH: 0.42, fontSize: 11, hl: new Set(['5,0', '5,3']) });
note(s, 'Each path wins one job; together they cover the SoW. A is retired; D (CharLS) is replaced by D+.', 4.3);

s = content('Comparison', 'Open Questions', 'Need answers from the SoW owner');
table(s, [
  ['#', 'Question', 'Impact'],
  ['1', 'Does “perfect reconstruction” apply at 2 Mbps, or only to a lossless mode?', 'Decides if the 2 Mbps stream must be bit-exact (impossible at ~79:1)'],
  ['2', 'Frame-rate target', 'Budget per frame (8,333 B at 30 fps) and the latency limit'],
  ['3', 'Memory limit on Jetson', 'Static-memory design; current encoder peaks at 20–22 MB'],
  ['4', 'Is a guaranteed max error required?', 'Whether the two-layer mode becomes default'],
  ['5', 'Is a C API required?', 'Wrapper work for MVP'],
], { colW: [0.4, 5.0, 3.9], rowH: 0.48, fontSize: 10.5, align: 'left' });

s = content('Comparison', 'Next Steps', 'In priority order');
bullets(s, [
  'Build and measure on Jetson Orin Nano: latency, memory, sustained fps',
  'Add a C API (extern "C") and preallocated buffers',
  'Speed up D+ toward CharLS speed: lookup tables, bit-depth-specific code',
  'Native fixed-rate control for D+ and two-layer (predict NEAR / split from the last frame)',
  'Early start: allocate bits per tile row so encoding overlaps sensor readout',
  'Get answers to the five open questions',
], { fontSize: 14 });

// =============================================================== closing
section('Closing');
titleSlide('RCI SoW PROBLEM 4', 'ewc — Final Architecture',
  'Wavelet for 2 Mbps  ·  D+ for lossless  ·  Two-layer for a bound\nA retired  ·  D replaced by D+',
  'Code and results: branch exp-2026-11-05  ·  docs/ARCHITECTURES_EXPLAINED.md');

(async () => {
  await pres.writeFile({ fileName: 'ewc_runs_and_architectures.pptx' });
  await applyTheme('ewc_runs_and_architectures.pptx', THEME);
  console.log('written');
})();
