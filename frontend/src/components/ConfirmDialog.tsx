import { useEffect, useRef } from 'react'
import { DangerButton, SecondaryButton } from './Form'

export function ConfirmDialog({
  open,
  title,
  description,
  confirmLabel,
  cancelLabel,
  confirming,
  onConfirm,
  onCancel,
}: {
  open: boolean
  title: string
  description: string
  confirmLabel: string
  cancelLabel: string
  confirming?: boolean
  onConfirm: () => void
  onCancel: () => void
}) {
  const ref = useRef<HTMLDialogElement>(null)

  useEffect(() => {
    const el = ref.current
    if (!el) return
    if (open && !el.open) {
      if (typeof el.showModal === 'function') el.showModal()
      else el.setAttribute('open', '')
    }
    if (!open && el.open) {
      if (typeof el.close === 'function') el.close()
      else el.removeAttribute('open')
    }
  }, [open])

  return (
    <dialog
      ref={ref}
      onCancel={(e) => {
        e.preventDefault()
        if (!confirming) onCancel()
      }}
      className="m-auto w-[min(92vw,24rem)] rounded-2xl border border-border bg-surface p-0 text-text shadow-[0_20px_60px_rgba(0,0,0,0.45)] backdrop:bg-black/55 open:flex open:flex-col"
    >
      <div className="space-y-2 px-5 pt-5 pb-4">
        <h2 className="text-lg font-semibold text-text-bright">{title}</h2>
        <p className="text-sm text-muted">{description}</p>
      </div>
      <div className="flex flex-col gap-2 border-t border-border px-5 py-4 sm:flex-row-reverse">
        <DangerButton
          type="button"
          loading={confirming}
          onClick={onConfirm}
          className="sm:w-auto sm:min-w-[8rem] sm:px-5"
        >
          {confirmLabel}
        </DangerButton>
        <SecondaryButton
          type="button"
          disabled={confirming}
          onClick={onCancel}
          className="sm:w-auto sm:min-w-[8rem] sm:px-5"
        >
          {cancelLabel}
        </SecondaryButton>
      </div>
    </dialog>
  )
}
