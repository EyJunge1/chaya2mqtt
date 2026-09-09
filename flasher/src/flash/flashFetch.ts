/** Sidecar and binary must skip HTTP cache so SHA and payload stay the same generation (RC-FE-04). */
export function firmwareFetchInit(): RequestInit {
  return { cache: "no-store" };
}
