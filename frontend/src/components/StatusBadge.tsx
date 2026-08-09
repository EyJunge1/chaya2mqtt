import { useI18n } from '../i18n'

export function StatusBadge({
  ok,
  label,
  detailOk,
  detailBad,
}: {
  ok: boolean
  label: string
  detailOk?: string
  detailBad?: string
}) {
  const { t } = useI18n()
  const detail = ok
    ? (detailOk ?? t('status.connected'))
    : (detailBad ?? t('status.disconnected'))

  return (
    <span className="group relative inline-flex">
      <span
        tabIndex={0}
        aria-label={`${label}: ${detail}`}
        className="inline-flex cursor-default items-center gap-1.5 rounded-full border border-border bg-surface px-2.5 py-1 text-xs font-semibold text-text-bright transition hover:border-accent/40 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-bg"
      >
        <span
          className={`size-2 rounded-full ${ok ? 'bg-status-ok' : 'bg-danger'}`}
          aria-hidden
        />
        {label}
      </span>
      <span
        role="tooltip"
        className="pointer-events-none absolute bottom-full left-1/2 z-20 mb-2 w-max max-w-[14rem] -translate-x-1/2 rounded-lg border border-border bg-surface px-2.5 py-1.5 text-xs font-normal text-text-bright opacity-0 shadow-lg transition group-hover:opacity-100 group-focus-within:opacity-100"
      >
        {detail}
      </span>
    </span>
  )
}
