/** Join class names, skipping falsy values. Accepts HTML `class` / clsx-like values. */
export function cn(...parts: unknown[]): string {
  const out: string[] = [];
  for (const part of parts) {
    append(part, out);
  }
  return out.join(" ");
}

function append(value: unknown, out: string[]): void {
  if (!value) return;
  if (typeof value === "string" || typeof value === "number" || typeof value === "bigint") {
    out.push(String(value));
    return;
  }
  if (Array.isArray(value)) {
    for (const item of value) append(item, out);
    return;
  }
  if (typeof value === "object") {
    for (const [key, enabled] of Object.entries(value as Record<string, unknown>)) {
      if (enabled) out.push(key);
    }
  }
}
