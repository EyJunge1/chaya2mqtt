import { useEffect, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { api } from '../api/client'
import type { WifiConnectStatus } from '../api/types'
import { Panel } from '../components/Card'
import { DangerButton, PrimaryButton } from '../components/Form'

export function WifiTestingPage({ onToast }: { onToast: (msg: string) => void }) {
  const navigate = useNavigate()
  const [status, setStatus] = useState<WifiConnectStatus>({ state: 'testing', ssid: '' })
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    let alive = true
    const tick = async () => {
      try {
        const s = await api.getWifiConnectStatus()
        if (alive) setStatus(s)
      } catch {
        /* keep polling */
      }
    }
    void tick()
    const id = window.setInterval(() => void tick(), 700)
    return () => {
      alive = false
      window.clearInterval(id)
    }
  }, [])

  async function commit() {
    setBusy(true)
    try {
      const res = await api.commitWifiConnect()
      if (!res.ok) {
        onToast('Übernehmen fehlgeschlagen')
        return
      }
      onToast('WLAN gespeichert — Neustart')
      navigate('/')
    } catch {
      onToast('Übernehmen fehlgeschlagen')
    } finally {
      setBusy(false)
    }
  }

  async function abort() {
    setBusy(true)
    try {
      await api.abortWifiConnect()
      navigate('/wifi')
    } catch {
      onToast('Abbruch fehlgeschlagen')
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="space-y-4">
      <Panel title="Verbindungstest">
        <p className="mb-2 text-sm text-muted">
          SSID: <span className="text-text-bright">{status.ssid || '…'}</span>
        </p>
        <p className="text-sm text-text-bright">
          {status.state === 'testing' && 'Teste Verbindung…'}
          {status.state === 'ok' && 'Verbindung erfolgreich'}
          {status.state === 'fail' && 'Verbindung fehlgeschlagen'}
          {status.state === 'idle' && 'Kein aktiver Test'}
        </p>
      </Panel>
      <div className="space-y-3">
        <PrimaryButton disabled={busy || status.state !== 'ok'} onClick={() => void commit()}>
          Speichern & neu starten
        </PrimaryButton>
        <DangerButton disabled={busy} onClick={() => void abort()}>
          Abbrechen
        </DangerButton>
      </div>
    </div>
  )
}
