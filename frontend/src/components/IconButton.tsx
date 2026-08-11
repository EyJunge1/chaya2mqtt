import type { AnchorHTMLAttributes, ButtonHTMLAttributes, ReactNode } from "react";
import { cn } from "../ui/cn";
import { HOVER_SURFACE } from "../ui/styles";

type Shared = {
  children: ReactNode;
  variant?: "ghost" | "bordered";
  size?: "sm" | "md";
  className?: string;
  title?: string;
  "aria-label": string;
};

const sizeClass = {
  sm: "size-8",
  md: "size-9",
} as const;

const variantClass = {
  ghost: cn("text-muted", HOVER_SURFACE),
  bordered: cn("border border-border bg-surface text-muted", HOVER_SURFACE),
} as const;

function baseClass(variant: "ghost" | "bordered", size: "sm" | "md", className?: string) {
  return cn(
    "inline-flex shrink-0 items-center justify-center rounded-lg transition focus-ring",
    sizeClass[size],
    variantClass[variant],
    className,
  );
}

export function IconButton({
  children,
  variant = "ghost",
  size = "md",
  className,
  type = "button",
  ...props
}: Shared & ButtonHTMLAttributes<HTMLButtonElement>) {
  return (
    <button type={type} className={baseClass(variant, size, className)} {...props}>
      {children}
    </button>
  );
}

export function IconLink({
  children,
  variant = "ghost",
  size = "md",
  className,
  ...props
}: Shared & AnchorHTMLAttributes<HTMLAnchorElement>) {
  return (
    <a className={baseClass(variant, size, className)} {...props}>
      {children}
    </a>
  );
}
