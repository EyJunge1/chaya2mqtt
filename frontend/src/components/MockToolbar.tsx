import { RotateCcw, X } from 'lucide-react'
import { useState } from 'react'
import { useLocation, useNavigate } from 'react-router-dom'

const scenarios = [
  { id: 'sta-connected', label: 'STA verbunden', path: '/' },
  { id: 'sta-auth', label: 'STA + Auth (Login)', path: '/auth' },
  { id: 'ap-setup', label: 'AP Setup', path: '/wifi' },
  { id: 'offline', label: 'Offline', path: '/' },
] as const

const pages = [
  { path: '/', label: 'Dashboard' },
  { path: '/wifi', label: 'WLAN' },
  { path: '/mqtt', label: 'MQTT' },
  { path: '/pairing', label: 'Pairing' },
  { path: '/settings', label: 'Einstellungen' },
  { path: '/update', label: 'Update' },
] as const

export function MockToolbar({ onChanged }: { onChanged: () => Promise<void> }) {
  const [open, setOpen] = useState(true)
  const [busy, setBusy] = useState(false)
  const navigate = useNavigate()
  const location = useLocation()
  if (!import.meta.env.DEV) return null

  async function setScenario(scenario: (typeof scenarios)[number]) {
    setBusy(true)
    try {
      const body = new URLSearchParams({ scenario: scenario.id })
      const response = await fetch('/api/_mock/scenario', { method: 'POST', body })
      if (!response.ok) throw new Error(`scenario failed (${response.status})`)
      await onChanged()
      navigate(scenario.path)
    } finally {
      setBusy(false)
    }
  }

  async function reset() {
    setBusy(true)
    try {
      const response = await fetch('/api/_mock/reset', { method: 'POST' })
      if (!response.ok) throw new Error(`reset failed (${response.status})`)
      await onChanged()
      navigate('/')
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="fixed right-3 top-3 z-40 w-[min(15rem,calc(100vw-1.5rem))] rounded-xl border border-border bg-surface/95 p-2 text-xs shadow-lg backdrop-blur">
      <button
        type="button"
        className="flex w-full items-center justify-between px-1 py-0.5 text-left font-semibold text-accent"
        onClick={() => setOpen((v) => !v)}
      >
        <span>Simulator</span>
        {open ? <X size={14} /> : <span>Öffnen</span>}
      </button>
      {open ? (
        <div className="mt-2 space-y-3 border-t border-border pt-2">
          <section>
            <p className="mb-1 px-2 text-[10px] font-semibold uppercase tracking-wider text-muted">
              Gerätezustand
            </p>
            <div className="space-y-1">
              {scenarios.map((scenario) => (
                <button
                  key={scenario.id}
                  type="button"
                  disabled={busy}
                  className="block w-full rounded-md px-2 py-1.5 text-left text-muted hover:bg-surface-hover hover:text-text-bright disabled:opacity-50"
                  onClick={() => void setScenario(scenario)}
                >
                  {scenario.label}
                </button>
              ))}
            </div>
          </section>

          <section>
            <p className="mb-1 px-2 text-[10px] font-semibold uppercase tracking-wider text-muted">
              Seite öffnen
            </p>
            <div className="grid grid-cols-2 gap-1">
              {pages.map((page) => (
                <button
                  key={page.path}
                  type="button"
                  disabled={busy}
                  className={`rounded-md px-2 py-1.5 text-left ${
                    location.pathname === page.path
                      ? 'bg-accent/15 text-accent'
                      : 'text-muted hover:bg-surface-hover hover:text-text-bright'
                  }`}
                  onClick={() => navigate(page.path)}
                >
                  {page.label}
                </button>
              ))}
            </div>
          </section>

          <button
            type="button"
            disabled={busy}
            className="flex w-full items-center gap-1.5 rounded-md border border-border px-2 py-1.5 text-muted hover:bg-surface-hover hover:text-text-bright disabled:opacity-50"
            onClick={() => void reset()}
          >
            <RotateCcw size={12} />
            Simulator zurücksetzen
          </button>
          <p className="px-2 text-[10px] leading-relaxed text-muted">
            Login-Code im Simulator: <strong className="text-text-bright">123456</strong>
          </p>
        </div>
      ) : null}
    </div>
  )
}
