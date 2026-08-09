import { useEffect } from 'react'
import { CircleCheck, X } from 'lucide-react'

export function Toast({
  message,
  onClose,
}: {
  message: string | null
  onClose: () => void
}) {
  useEffect(() => {
    if (!message) return
    const t = window.setTimeout(onClose, 3200)
    return () => window.clearTimeout(t)
  }, [message, onClose])

  if (!message) return null

  return (
    <div
      role="status"
      aria-live="polite"
      className="fixed right-4 bottom-4 left-4 z-50 overflow-hidden rounded-xl border border-border bg-surface/95 shadow-[0_12px_40px_rgba(0,0,0,0.45)] backdrop-blur-sm animate-[toast-in_180ms_ease-out] sm:left-auto sm:w-[22rem]"
    >
      <div className="flex items-center gap-3 px-3.5 py-3">
        <span className="flex size-8 shrink-0 items-center justify-center rounded-lg bg-accent/15 text-accent">
          <CircleCheck size={17} strokeWidth={2.25} />
        </span>
        <p className="min-w-0 flex-1 text-sm leading-snug font-medium text-text-bright">
          {message}
        </p>
        <button
          type="button"
          aria-label="Schließen"
          onClick={onClose}
          className="flex size-7 shrink-0 items-center justify-center rounded-md text-muted transition hover:bg-surface-hover hover:text-text-bright"
        >
          <X size={15} />
        </button>
      </div>
      <div className="h-0.5 origin-left bg-accent/80 animate-[toast-progress_3200ms_linear_forwards]" />
    </div>
  )
}
