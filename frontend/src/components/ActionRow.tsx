import type { ReactNode } from "react";
import { cn } from "../ui/cn";

/** Stacks actions on mobile, places them in a row from `sm` up. */
export function ActionRow({ children, className }: { children: ReactNode; className?: string }) {
  return <div className={cn("flex flex-col gap-2 sm:flex-row", className)}>{children}</div>;
}
