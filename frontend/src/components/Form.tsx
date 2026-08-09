import type { InputHTMLAttributes, ReactNode } from 'react'

export function Field({
  label,
  children,
  hint,
}: {
  label: string
  children: ReactNode
  hint?: string
}) {
  return (
    <label className="block">
      <span className="mb-1.5 block text-sm font-semibold text-text-bright">{label}</span>
      {children}
      {hint ? <span className="mt-1 block text-xs text-muted">{hint}</span> : null}
    </label>
  )
}

export function TextInput(props: InputHTMLAttributes<HTMLInputElement>) {
  return (
    <input
      {...props}
      className={`w-full rounded-lg border border-border bg-bg px-3 py-2.5 text-text outline-none transition focus:border-accent ${props.className ?? ''}`}
    />
  )
}

export function PrimaryButton({
  children,
  ...props
}: React.ButtonHTMLAttributes<HTMLButtonElement>) {
  return (
    <button
      {...props}
      className={`w-full rounded-xl bg-accent px-4 py-3.5 text-base font-semibold text-bg transition enabled:hover:opacity-90 disabled:opacity-50 ${props.className ?? ''}`}
    >
      {children}
    </button>
  )
}

export function DangerButton({
  children,
  ...props
}: React.ButtonHTMLAttributes<HTMLButtonElement>) {
  return (
    <button
      {...props}
      className={`w-full rounded-xl border border-danger/40 bg-danger/10 px-4 py-3.5 text-base font-semibold text-danger transition enabled:hover:bg-danger/20 disabled:opacity-50 ${props.className ?? ''}`}
    >
      {children}
    </button>
  )
}
