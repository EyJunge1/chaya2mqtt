#!/usr/bin/env node
/**
 * Render the README banner (logical 1200×300, exported @2× → 2400×600).
 *
 * Usage: node scripts/docs/render_banner.mjs
 * Writes `.github/pics/banner.png`
 *
 * Matches the web GUI brand mark (sidebar / boot):
 *   ICON_WELL (bg-accent/15 text-accent) + Lucide Heart fill="currentColor"
 *   + title in system-ui font-bold text-text-bright
 *
 * Colors: frontend/src/index.css `[data-theme="light"] --ui-*`
 */

import { execFileSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const outPath = path.join(repoRoot, ".github/pics/banner.png");
const cssPath = path.join(repoRoot, "frontend/src/index.css");
const LUCIDE_REF = "0.468.0";
const LUCIDE_HEART =
  `https://raw.githubusercontent.com/lucide-icons/lucide/${LUCIDE_REF}/icons/heart.svg`;
/** Lucide heart path — https://lucide.dev/icons/heart */
const LUCIDE_HEART_PATH =
  'd="M19 14c1.49-1.46 3-3.21 3-5.5A5.5 5.5 0 0 0 16.5 3c-1.76 0-3 .5-4.5 2-1.5-1.5-2.74-2-4.5-2A5.5 5.5 0 0 0 2 8.5c0 2.3 1.5 4.05 3 5.5l7 7Z"';
/**
 * Frontend uses system-ui / -apple-system (SF on macOS). resvg cannot load
 * SFNS.ttf, so Helvetica Neue is the closest system grotesque that supports 700.
 */
const FONT_FAMILY = "Helvetica Neue";
const FONT_FILES = [
  "/System/Library/Fonts/HelveticaNeue.ttc",
  "/System/Library/Fonts/Supplemental/Arial.ttf",
  "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
];

const W = 1200;
const H = 300;
const WELL = 96;
const HEART = 44;
const TITLE = 52;
const TAG = 22;
const SCALE = 2;

function loadColors() {
  const css = fs.readFileSync(cssPath, "utf8");
  const block = css.match(/\[data-theme="light"\]\s*\{([\s\S]*?)\n\}/);
  if (!block) throw new Error("light theme not found in frontend/src/index.css");
  const t = {};
  for (const m of block[1].matchAll(/--ui-([\w-]+)\s*:\s*(#[0-9a-fA-F]{3,8})\s*;/g)) {
    t[m[1]] = m[2].toLowerCase();
  }
  for (const k of ["bg", "border", "text-bright", "muted", "accent"]) {
    if (!t[k]) throw new Error(`missing --ui-${k}`);
  }
  return {
    bg: t.bg,
    border: t.border,
    textBright: t["text-bright"],
    muted: t.muted,
    accent: t.accent,
  };
}

/** Brand mark: rounded accent well + filled Lucide heart (Layout.svelte / DeviceRoot). */
function brandMark(x, y, well, heart, accent) {
  const pad = (well - heart) / 2;
  const rx = Math.round(well * 0.28);
  return `
  <rect x="${x}" y="${y}" width="${well}" height="${well}" rx="${rx}"
        fill="${accent}" fill-opacity="0.15"/>
  <svg x="${x + pad}" y="${y + pad}" width="${heart}" height="${heart}" viewBox="0 0 24 24"
      fill="${accent}" stroke="${accent}" stroke-width="1.5"
      stroke-linecap="round" stroke-linejoin="round">
    <path ${LUCIDE_HEART_PATH}/>
  </svg>`;
}

async function main() {
  const colors = loadColors();
  const heartRes = await fetch(LUCIDE_HEART);
  if (!heartRes.ok) throw new Error(`Lucide heart fetch failed: ${heartRes.status}`);
  const heartRaw = await heartRes.text();
  if (!heartRaw.includes(LUCIDE_HEART_PATH.slice(0, 24))) {
    throw new Error("unexpected Lucide heart.svg contents (pin mismatch)");
  }

  const cx = W / 2;
  const cy = H / 2;
  const gap = 28;
  const textW = Math.round(TITLE * "Chaya2MQTT".length * 0.55);
  const groupW = WELL + gap + textW;
  const groupX = Math.round(cx - groupW / 2);
  const wellY = Math.round(cy - WELL / 2);

  const svg = `<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">
  <rect width="${W}" height="${H}" fill="${colors.bg}"/>
  <rect x="2" y="2" width="${W - 4}" height="${H - 4}" rx="20"
        fill="${colors.bg}" stroke="${colors.border}" stroke-width="2"/>
  <g transform="translate(${groupX}, 0)">
    ${brandMark(0, wellY, WELL, HEART, colors.accent)}
    <text x="${WELL + gap}" y="${Math.round(cy - TITLE * 0.22)}" text-anchor="start"
      font-family="${FONT_FAMILY}" font-weight="700" font-size="${TITLE}"
      fill="${colors.textBright}" dominant-baseline="middle">Chaya2MQTT</text>
    <text x="${WELL + gap}" y="${Math.round(cy + TAG * 1.55)}" text-anchor="start"
      font-family="${FONT_FAMILY}" font-weight="400" font-size="${TAG}"
      fill="${colors.muted}" dominant-baseline="middle">send a little heart</text>
  </g>
</svg>`;

  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "chaya-banner-"));
  try {
    execFileSync("npm", ["init", "-y"], { cwd: tmp, stdio: "ignore" });
    execFileSync("npm", ["install", "@resvg/resvg-js@2.6.2"], { cwd: tmp, stdio: "inherit" });
    const { Resvg } = await import(path.join(tmp, "node_modules/@resvg/resvg-js/index.js"));
    const fontFiles = FONT_FILES.filter((f) => fs.existsSync(f));
    if (!fontFiles.length) throw new Error("no system UI fonts found (Helvetica Neue / Arial)");
    const rendered = new Resvg(svg, {
      fitTo: { mode: "width", value: W * SCALE },
      font: {
        fontFiles,
        loadSystemFonts: false,
        defaultFontFamily: FONT_FAMILY,
      },
    }).render();
    fs.mkdirSync(path.dirname(outPath), { recursive: true });
    fs.writeFileSync(outPath, rendered.asPng());
    console.log(`wrote ${path.relative(repoRoot, outPath)} (${rendered.width}×${rendered.height})`);
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
}

await main();
