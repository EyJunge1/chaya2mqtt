import { LoaderCircle } from 'lucide-react'
import { Alert } from './Alert'
import { SecondaryButton } from './Form'

export function LoadingBlock({ label }: { label: string }) {
  return (
    <div
      role="status"
      aria-busy="true"
      className="flex items-center justify-center gap-2 rounded-xl border border-border bg-surface px-4 py-8 text-sm text-muted"
    >
      <LoaderCircle size={18} className="animate-spin text-accent" aria-hidden />
      {label}
    </div>
  )
}

export function ErrorBlock({
  title,
  message,
  retryLabel,
  onRetry,
}: {
  title: string
  message: string
  retryLabel: string
  onRetry: () => void
}) {
  return (
    <div className="space-y-3">
      <Alert variant="error" title={title}>
        {message}
      </Alert>
      <SecondaryButton type="button" onClick={onRetry}>
        {retryLabel}
      </SecondaryButton>
    </div>
  )
}
