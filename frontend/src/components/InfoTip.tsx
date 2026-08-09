import { Info } from 'lucide-react'

const focusRing =
  'focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-bg'

export function InfoTip({ text, className = '' }: { text: string; className?: string }) {
  return (
    <span className={`group relative inline-flex ${className}`}>
      <button
        type="button"
        tabIndex={0}
        aria-label={text}
        className={`inline-flex size-5 items-center justify-center rounded-full text-muted transition hover:bg-accent/10 hover:text-accent ${focusRing}`}
        onClick={(e) => e.preventDefault()}
      >
        <Info size={14} strokeWidth={2.25} aria-hidden />
      </button>
      <span
        role="tooltip"
        className="pointer-events-none absolute bottom-full left-1/2 z-20 mb-2 w-max max-w-[16rem] -translate-x-1/2 rounded-lg border border-border bg-surface px-2.5 py-1.5 text-left text-xs font-normal text-text-bright opacity-0 shadow-lg transition group-hover:opacity-100 group-focus-within:opacity-100"
      >
        {text}
      </span>
    </span>
  )
}
