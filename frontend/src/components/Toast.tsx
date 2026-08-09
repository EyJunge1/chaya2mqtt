import { useEffect } from 'react'
import { X } from 'lucide-react'

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
    <div className="fixed bottom-4 left-1/2 z-50 w-[min(92vw,28rem)] -translate-x-1/2 rounded-xl border border-border bg-surface px-4 py-3 shadow-lg">
      <div className="flex items-start gap-3">
        <p className="flex-1 text-sm text-text-bright">{message}</p>
        <button
          type="button"
          aria-label="Schließen"
          onClick={onClose}
          className="text-muted hover:text-text-bright"
        >
          <X size={16} />
        </button>
      </div>
    </div>
  )
}
