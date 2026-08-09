import { LoaderCircle } from "lucide-react";
import type {
  ButtonHTMLAttributes,
  InputHTMLAttributes,
  ReactNode,
  SelectHTMLAttributes,
} from "react";
import { InfoTip } from "./InfoTip";

const focusRing =
  "focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-bg";

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
      className={`w-full rounded-lg border border-border bg-bg px-3 py-2.5 text-text outline-none transition focus:border-accent ${focusRing} ${props.className ?? ""}`}
    />
  );
}

export function SelectInput(props: SelectHTMLAttributes<HTMLSelectElement>) {
  return (
    <select
      {...props}
      className={`w-full rounded-lg border border-border bg-bg px-3 py-2.5 text-text outline-none transition focus:border-accent ${focusRing} ${props.className ?? ""}`}
    />
  );
}

type ButtonProps = ButtonHTMLAttributes<HTMLButtonElement> & {
  loading?: boolean;
};

function ButtonContent({ loading, children }: { loading?: boolean; children: ReactNode }) {
  return (
    <span className="inline-flex items-center justify-center gap-2">
      {loading ? <LoaderCircle size={18} className="animate-spin" aria-hidden /> : null}
      {children}
    </span>
  );
}

export function PrimaryButton({ children, loading, disabled, className, ...props }: ButtonProps) {
  return (
    <button
      {...props}
      disabled={disabled || loading}
      aria-busy={loading || undefined}
      className={`w-full rounded-xl bg-accent px-4 py-3.5 text-base font-semibold text-bg transition enabled:hover:opacity-90 disabled:opacity-50 ${focusRing} ${className ?? ""}`}
    >
      <ButtonContent loading={loading}>{children}</ButtonContent>
    </button>
  );
}

export function SecondaryButton({ children, loading, disabled, className, ...props }: ButtonProps) {
  return (
    <button
      {...props}
      disabled={disabled || loading}
      aria-busy={loading || undefined}
      className={`w-full rounded-xl border border-border bg-surface px-4 py-3.5 text-base font-semibold text-text-bright transition enabled:hover:bg-surface-hover disabled:opacity-50 ${focusRing} ${className ?? ""}`}
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
      className={`inline-flex items-center justify-center gap-1 rounded-lg px-2 py-1.5 text-sm font-semibold text-accent transition enabled:hover:bg-accent/10 disabled:opacity-50 ${focusRing} ${className ?? ""}`}
    >
      <ButtonContent loading={loading}>{children}</ButtonContent>
    </button>
  );
}

export function DangerButton({ children, loading, disabled, className, ...props }: ButtonProps) {
  return (
    <button
      {...props}
      disabled={disabled || loading}
      aria-busy={loading || undefined}
      className={`w-full rounded-xl border border-danger/40 bg-danger/10 px-4 py-3.5 text-base font-semibold text-danger transition enabled:hover:bg-danger/20 disabled:opacity-50 ${focusRing} ${className ?? ""}`}
    >
      <ButtonContent loading={loading}>{children}</ButtonContent>
    </button>
  );
}
