export function ProgressBar({
  value,
  label,
}: {
  /** 0–100, or null for indeterminate. */
  value: number | null;
  label?: string;
}) {
  const pct = value == null ? null : Math.min(100, Math.max(0, value));
  return (
    <div
      role="progressbar"
      aria-valuemin={0}
      aria-valuemax={100}
      aria-valuenow={pct ?? undefined}
      aria-label={label}
      className="h-2 overflow-hidden rounded-full bg-border"
    >
      <div
        className="h-full rounded-full bg-accent transition-all"
        style={{ width: `${pct ?? 10}%` }}
      />
    </div>
  );
}
