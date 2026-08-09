import { useEffect } from 'react'
import { CircleAlert, CircleCheck, Info, OctagonX, X } from 'lucide-react'
import { useI18n } from '../i18n'

export type ToastVariant = 'success' | 'error' | 'warning' | 'info'

export type ToastMessage = {
  text: string
  variant: ToastVariant
} | null

export type ShowToast = (text: string, variant?: ToastVariant) => void

const styles: Record<
  ToastVariant,
  { iconBg: string; iconColor: string; bar: string; Icon: typeof CircleCheck }
> = {
  success: {
    iconBg: 'bg-accent/15',
    iconColor: 'text-accent',
    bar: 'bg-accent/80',
    Icon: CircleCheck,
  },
  error: {
    iconBg: 'bg-danger/15',
    iconColor: 'text-danger',
    bar: 'bg-danger/80',
    Icon: OctagonX,
  },
  warning: {
    iconBg: 'bg-warning/15',
    iconColor: 'text-warning',
    bar: 'bg-warning/80',
    Icon: CircleAlert,
  },
  info: {
    iconBg: 'bg-muted/20',
    iconColor: 'text-muted',
    bar: 'bg-muted/80',
    Icon: Info,
  },
}

export function Toast({
  message,
  onClose,
}: {
  message: ToastMessage
  onClose: () => void
}) {
  useEffect(() => {
    if (!message) return
    const t = window.setTimeout(onClose, 3200)
    return () => window.clearTimeout(t)
  }, [message, onClose])

  const { t } = useI18n()

  if (!message) return null

  const style = styles[message.variant]
  const Icon = style.Icon
  const assertive = message.variant === 'error' || message.variant === 'warning'

  return (
    <div
      role={assertive ? 'alert' : 'status'}
      aria-live={assertive ? 'assertive' : 'polite'}
      className="fixed right-4 bottom-[max(1rem,env(safe-area-inset-bottom))] left-4 z-50 overflow-hidden rounded-xl border border-border bg-surface/95 shadow-[0_12px_40px_rgba(0,0,0,0.45)] backdrop-blur-sm animate-[toast-in_180ms_ease-out] sm:left-auto sm:w-[22rem]"
    >
      <div className="flex items-center gap-3 px-3.5 py-3">
        <span
          className={`flex size-8 shrink-0 items-center justify-center rounded-lg ${style.iconBg} ${style.iconColor}`}
        >
          <Icon size={17} strokeWidth={2.25} />
        </span>
        <p className="min-w-0 flex-1 text-sm leading-snug font-medium text-text-bright">
          {message.text}
        </p>
        <button
          type="button"
          aria-label={t('common.close')}
          onClick={onClose}
          className="flex size-7 shrink-0 items-center justify-center rounded-md text-muted transition hover:bg-surface-hover hover:text-text-bright focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-bg"
        >
          <X size={15} />
        </button>
      </div>
      <div
        className={`h-0.5 origin-left animate-[toast-progress_3200ms_linear_forwards] ${style.bar}`}
      />
    </div>
  )
}
