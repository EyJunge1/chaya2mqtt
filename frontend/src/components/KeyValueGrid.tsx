import type { ReactNode } from "react";
import { cn } from "../ui/cn";

export type KeyValueItem = {
  label: string;
  value: ReactNode;
  className?: string;
  span?: 1 | 2;
};

export function KeyValueGrid({ items, className }: { items: KeyValueItem[]; className?: string }) {
  return (
    <dl className={cn("grid gap-3 text-sm sm:grid-cols-2", className)}>
      {items.map((item) => (
        <div
          key={item.label}
          className={cn(item.span === 2 ? "sm:col-span-2" : undefined, item.className)}
        >
          <dt className="text-muted">{item.label}</dt>
          <dd className="font-semibold text-text-bright">{item.value}</dd>
        </div>
      ))}
    </dl>
  );
}
