/** Pure helpers for flasher firmware verification (TEST-08 / SEC-10). */

const SHA256_HEX_RE = /^[0-9a-fA-F]{64}$/;

export function isSha256Hex(value: string | undefined): boolean {
  return Boolean(value && SHA256_HEX_RE.test(value));
}

/**
 * Resolve a firmware part path against the manifest URL.
 * Rejects absolute / protocol-relative paths and cross-origin results (SEC-10).
 */
export function resolvePartUrl(partPath: string, manifestHref: string): string {
  if (/^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(partPath) || partPath.startsWith("//")) {
    throw new Error("Absolute firmware part path rejected");
  }
  const base = new URL(manifestHref);
  const resolved = new URL(partPath, base);
  if (resolved.origin !== base.origin) {
    throw new Error("Cross-origin firmware part path rejected");
  }
  return resolved.href;
}

/** Sidecar next to the binary: firmware.factory.bin → firmware.factory.sha256 */
export function sidecarUrlForPart(partUrl: string): string {
  if (/\.bin$/i.test(partUrl)) {
    return partUrl.replace(/\.bin$/i, ".sha256");
  }
  return `${partUrl}.sha256`;
}

export function parseSha256SidecarText(text: string): string | null {
  const hex = text.trim().split(/\s+/)[0] ?? "";
  if (!SHA256_HEX_RE.test(hex)) {
    return null;
  }
  return hex.toLowerCase();
}
