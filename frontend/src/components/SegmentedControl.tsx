import { cn } from "../ui/cn";
import { HOVER_ROW } from "../ui/styles";

export type SegmentOption<T extends string> = {
  value: T;
  label: string;
  testId?: string;
};

export function SegmentedControl<T extends string>({
  value,
  onChange,
  options,
  label,
}: {
  value: T;
  onChange: (value: T) => void;
  options: SegmentOption<T>[];
  label: string;
}) {
  return (
    <div
      role="radiogroup"
      aria-label={label}
      className="grid grid-cols-2 gap-1 rounded-xl border border-border bg-bg p-1"
    >
      {options.map((option) => {
        const active = value === option.value;
        return (
          <button
            key={option.value}
            type="button"
            role="radio"
            aria-checked={active}
            data-testid={option.testId}
            onClick={() => onChange(option.value)}
            className={cn(
              "rounded-lg border border-transparent px-3 py-2 text-sm font-semibold transition focus-ring",
              HOVER_ROW,
              active
                ? "bg-surface text-text-bright shadow-sm"
                : "text-muted hover:text-text-bright",
            )}
          >
            {option.label}
          </button>
        );
      })}
    </div>
  );
}
