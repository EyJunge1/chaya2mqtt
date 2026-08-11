import type {
  ButtonHTMLAttributes,
  InputHTMLAttributes,
  ReactNode,
  SelectHTMLAttributes,
} from "react";
import { Link, type LinkProps } from "react-router-dom";
import { cn } from "../ui/cn";
import { InfoTip } from "./InfoTip";
import { Spinner } from "./Spinner";

export function Field({
  label,
  children,
  hint,
}: {
  label: string;
  children: ReactNode;
  hint?: string;
}) {
  return (
    <label className="block">
      <span className="mb-1.5 flex items-center gap-1.5 text-sm font-semibold text-text-bright">
        {label}
        {hint ? <InfoTip text={hint} /> : null}
      </span>
      {children}
    </label>
  );
}

export function TextInput(props: InputHTMLAttributes<HTMLInputElement>) {
  return (
    <input
      {...props}
      className={cn(
        "w-full rounded-lg border border-border bg-bg px-3 py-2.5 text-text outline-none transition focus:border-accent",
        props.className,
      )}
    />
  );
}

export function SelectInput(props: SelectHTMLAttributes<HTMLSelectElement>) {
  return (
    <select
      {...props}
      className={cn(
        "w-full rounded-lg border border-border bg-bg px-3 py-2.5 text-text outline-none transition focus:border-accent",
        props.className,
      )}
    />
  );
}

export function Switch({
  checked,
  onChange,
  disabled,
  label,
}: {
  checked: boolean;
  onChange: (checked: boolean) => void;
  disabled?: boolean;
  label: string;
}) {
  return (
    <button
      type="button"
      role="switch"
      aria-checked={checked}
      aria-label={label}
      disabled={disabled}
      onClick={() => onChange(!checked)}
      className={cn(
        "relative inline-flex h-7 w-12 shrink-0 items-center rounded-full border transition enabled:hover:opacity-90 disabled:opacity-50 focus-ring",
        checked ? "border-accent bg-accent" : "border-border bg-surface",
      )}
    >
      <span
        aria-hidden
        className={cn(
          "inline-block h-5 w-5 transform rounded-full bg-bg shadow transition",
          checked ? "translate-x-6" : "translate-x-1",
        )}
      />
    </button>
  );
}

type ButtonWidth = "full" | "auto";

type ButtonProps = ButtonHTMLAttributes<HTMLButtonElement> & {
  loading?: boolean;
  width?: ButtonWidth;
};

function ButtonContent({ loading, children }: { loading?: boolean; children: ReactNode }) {
  return (
    <span className="inline-flex items-center justify-center gap-2">
      {loading ? <Spinner size={18} className="text-current" /> : null}
      {children}
    </span>
  );
}

function widthClass(width: ButtonWidth = "full"): string {
  return width === "full" ? "w-full" : "w-auto self-start";
}

export function PrimaryButton({
  children,
  loading,
  disabled,
  className,
  width = "full",
  ...props
}: ButtonProps) {
  return (
    <button
      {...props}
      disabled={disabled || loading}
      aria-busy={loading || undefined}
      className={cn(
        "rounded-xl bg-accent px-4 py-3.5 text-base font-semibold text-bg transition enabled:hover:opacity-90 disabled:opacity-50 focus-ring",
        widthClass(width),
        className,
      )}
    >
      <ButtonContent loading={loading}>{children}</ButtonContent>
    </button>
  );
}

export function SecondaryButton({
  children,
  loading,
  disabled,
  className,
  width = "full",
  ...props
}: ButtonProps) {
  return (
    <button
      {...props}
      disabled={disabled || loading}
      aria-busy={loading || undefined}
      className={cn(
        "rounded-xl border border-border bg-surface px-4 py-3.5 text-base font-semibold text-text-bright transition enabled:hover:bg-surface-hover disabled:opacity-50 focus-ring",
        widthClass(width),
        className,
      )}
    >
      <ButtonContent loading={loading}>{children}</ButtonContent>
    </button>
  );
}

export function GhostButton({ children, loading, disabled, className, ...props }: ButtonProps) {
  return (
    <button
      {...props}
      disabled={disabled || loading}
      aria-busy={loading || undefined}
      className={cn(
        "inline-flex items-center justify-center gap-1 rounded-lg px-2 py-1.5 text-sm font-semibold text-accent transition enabled:hover:bg-surface-hover disabled:opacity-50 focus-ring",
        className,
      )}
    >
      <ButtonContent loading={loading}>{children}</ButtonContent>
    </button>
  );
}

export function DangerButton({
  children,
  loading,
  disabled,
  className,
  width = "full",
  ...props
}: ButtonProps) {
  return (
    <button
      {...props}
      disabled={disabled || loading}
      aria-busy={loading || undefined}
      className={cn(
        "rounded-xl border border-danger/35 bg-danger/10 px-4 py-3.5 text-base font-semibold text-danger transition enabled:hover:bg-danger/20 disabled:opacity-50 focus-ring",
        widthClass(width),
        className,
      )}
    >
      <ButtonContent loading={loading}>{children}</ButtonContent>
    </button>
  );
}

type LinkButtonProps = LinkProps & {
  variant?: "primary" | "secondary" | "warning";
  className?: string;
  children: ReactNode;
};

export function LinkButton({
  children,
  variant = "secondary",
  className,
  ...props
}: LinkButtonProps) {
  const variantClass =
    variant === "primary"
      ? "bg-accent text-bg enabled:hover:opacity-90"
      : variant === "warning"
        ? "border border-warning/35 bg-surface text-text-bright hover:bg-surface-hover"
        : "border border-border bg-surface text-text-bright hover:bg-surface-hover";

  return (
    <Link
      {...props}
      className={cn(
        "inline-flex items-center justify-center rounded-lg px-3 py-1.5 text-sm font-semibold transition focus-ring",
        variantClass,
        className,
      )}
    >
      {children}
    </Link>
  );
}
