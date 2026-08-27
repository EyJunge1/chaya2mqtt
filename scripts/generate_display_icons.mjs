#!/usr/bin/env node
/**
 * Rasterize Lucide SVGs into Adafruit-GFX 1-bit PROGMEM bitmaps for the E-Ink panel.
 *
 * Usage (from repo root, once when icons change):
 *   node scripts/generate_display_icons.mjs
 *
 * Requires network access to fetch Lucide SVGs, and installs @resvg/resvg-js
 * into a temporary directory. The generated header is checked in so normal
 * PlatformIO builds need no SVG tooling.
 *
 * Lucide Icons — ISC License — https://lucide.dev / https://github.com/lucide-icons/lucide
 */

import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(__dirname, "..");
const outHeader = path.join(repoRoot, "src/display/icons_lucide.h");

/** Pin Lucide icon source for reproducible bitmaps. */
const LUCIDE_REF = "0.468.0";
const LUCIDE_BASE =
  `https://raw.githubusercontent.com/lucide-icons/lucide/${LUCIDE_REF}/icons`;

const ICONS = [
  {
    id: "heart",
    svgName: "heart",
    size: 162,
    strokeWidth: 2,
    fill: true,
    symbol: "kIconHeart",
  },
  {
    id: "heart_crack",
    svgName: "heart-crack",
    size: 162,
    strokeWidth: 2.25,
    fill: false,
    symbol: "kIconHeartCrack",
  },
  {
    id: "heart_off",
    svgName: "heart-off",
    size: 162,
    strokeWidth: 2.25,
    fill: false,
    symbol: "kIconHeartOff",
  },
  {
    id: "battery_full",
    svgName: "battery-full",
    size: 28,
    strokeWidth: 2.25,
    fill: false,
    symbol: "kIconBatteryFull",
  },
  {
    id: "battery_medium",
    svgName: "battery-medium",
    size: 28,
    strokeWidth: 2.25,
    fill: false,
    symbol: "kIconBatteryMedium",
  },
  {
    id: "battery_low",
    svgName: "battery-low",
    size: 28,
    strokeWidth: 2.25,
    fill: false,
    symbol: "kIconBatteryLow",
  },
  {
    id: "battery_warning",
    svgName: "battery-warning",
    size: 28,
    strokeWidth: 2.25,
    fill: false,
    symbol: "kIconBatteryWarning",
  },
  {
    id: "arrow_down",
    svgName: "arrow-down",
    size: 28,
    strokeWidth: 2.5,
    fill: false,
    symbol: "kIconArrowDown",
  },
  {
    id: "arrow_up",
    svgName: "arrow-up",
    size: 28,
    strokeWidth: 2.5,
    fill: false,
    symbol: "kIconArrowUp",
  },
];

function prepareSvg(raw, { strokeWidth, fill }) {
  let svg = raw
    .replace(/\bstroke="currentColor"/g, 'stroke="#000"')
    .replace(/\bfill="currentColor"/g, 'fill="#000"')
    .replace(/\bstroke-width="[^"]*"/g, `stroke-width="${strokeWidth}"`);

  if (fill) {
    // Filled Lucide-style heart: keep stroke for crisp edges, paint the path solid.
    svg = svg.replace(/\bfill="none"/g, 'fill="#000"');
    svg = svg.replace(
      /(<path\b[^>]*?)(\s*\/>|>)/g,
      (match, open, close) => {
        if (/\bfill=/.test(open)) {
          return match;
        }
        return `${open} fill="#000"${close}`;
      },
    );
  }
  return svg;
}

function formatByteArray(bytes, indent = "    ") {
  const parts = [];
  for (let i = 0; i < bytes.length; i++) {
    if (i % 12 === 0) {
      parts.push(`\n${indent}`);
    }
    parts.push(`0x${bytes[i].toString(16).padStart(2, "0").toUpperCase()},`);
    if (i % 12 !== 11 && i !== bytes.length - 1) {
      parts.push(" ");
    }
  }
  return parts.join("").trimEnd();
}

async function main() {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "chaya-lucide-"));
  try {
    execFileSync("npm", ["init", "-y"], { cwd: tmp, stdio: "ignore" });
    execFileSync("npm", ["install", "@resvg/resvg-js@2.6.2"], {
      cwd: tmp,
      stdio: "inherit",
    });
    const { Resvg } = await import(path.join(tmp, "node_modules/@resvg/resvg-js/index.js"));

    const blocks = [];
    const hashes = [];

    for (const icon of ICONS) {
      const url = `${LUCIDE_BASE}/${icon.svgName}.svg`;
      const res = await fetch(url);
      if (!res.ok) {
        throw new Error(`Failed to fetch ${url}: ${res.status}`);
      }
      const raw = await res.text();
      const svg = prepareSvg(raw, icon);
      const rendered = new Resvg(svg, {
        fitTo: { mode: "width", value: icon.size },
        background: "rgba(0,0,0,0)",
      }).render();
      const width = rendered.width;
      const height = rendered.height;
      const maybe = rendered.pixels;
      if (!(maybe instanceof Uint8Array)) {
        throw new Error(
          `resvg render for ${icon.id} has no pixels buffer; install matching @resvg/resvg-js`,
        );
      }
      const { bytes, rowBytes, trimW, trimH } = trimAndPack(maybe, width, height);

      hashes.push(
        `${icon.id}:${createHash("sha256").update(bytes).digest("hex").slice(0, 16)}`,
      );

      blocks.push(`/** Lucide "${icon.svgName}" @ ${LUCIDE_REF}, ${trimW}x${trimH} 1-bit. */
static constexpr int16_t ${icon.symbol}W = ${trimW};
static constexpr int16_t ${icon.symbol}H = ${trimH};
static const uint8_t ${icon.symbol}[] PROGMEM = {${formatByteArray(bytes)}
};
`);
      console.log(
        `  ${icon.id}: ${width}x${height} -> ${trimW}x${trimH} (${bytes.length} bytes, row=${rowBytes})`,
      );
    }

    const header = `#pragma once

/**
 * Auto-generated Lucide icon bitmaps for the 1.54\" E-Ink panel (Adafruit GFX MSB format).
 * Do not edit by hand — regenerate with: node scripts/generate_display_icons.mjs
 *
 * Source: Lucide Icons (${LUCIDE_REF}) — https://lucide.dev
 * License: ISC — https://github.com/lucide-icons/lucide/blob/main/LICENSE
 *
 * Content digests: ${hashes.join(", ")}
 */

#include <Arduino.h>
#include <cstdint>

${blocks.join("\n")}`;

    fs.writeFileSync(outHeader, header);
    console.log(`Wrote ${path.relative(repoRoot, outHeader)}`);
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
}

function trimAndPack(rgba, width, height) {
  let minX = width;
  let minY = height;
  let maxX = -1;
  let maxY = -1;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const i = (y * width + x) * 4;
      const a = rgba[i + 3];
      const lum = (rgba[i] + rgba[i + 1] + rgba[i + 2]) / 3;
      if (a >= 128 && lum < 160) {
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
      }
    }
  }
  if (maxX < 0) {
    throw new Error("Icon rendered empty");
  }
  // One-pixel margin so strokes are not clipped.
  minX = Math.max(0, minX - 1);
  minY = Math.max(0, minY - 1);
  maxX = Math.min(width - 1, maxX + 1);
  maxY = Math.min(height - 1, maxY + 1);
  const trimW = maxX - minX + 1;
  const trimH = maxY - minY + 1;
  const rowBytes = Math.ceil(trimW / 8);
  const out = new Uint8Array(rowBytes * trimH);
  for (let y = 0; y < trimH; y++) {
    for (let x = 0; x < trimW; x++) {
      const sx = minX + x;
      const sy = minY + y;
      const i = (sy * width + sx) * 4;
      const a = rgba[i + 3];
      const lum = (rgba[i] + rgba[i + 1] + rgba[i + 2]) / 3;
      if (a >= 128 && lum < 160) {
        out[y * rowBytes + (x >> 3)] |= 0x80 >> (x & 7);
      }
    }
  }
  return {
    bytes: out,
    rowBytes,
    trimW,
    trimH,
  };
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
