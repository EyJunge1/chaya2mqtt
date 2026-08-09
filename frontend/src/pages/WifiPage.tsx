import { Lock, RefreshCw, Wifi } from 'lucide-react'
import { useCallback, useEffect, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { api } from '../api/client'
import type { DeviceInfo, WifiScanAp, WifiStatus } from '../api/types'
import { Panel } from '../components/Card'
import { Field, PrimaryButton, TextInput } from '../components/Form'
import { StatusBadge } from '../components/StatusBadge'

export function WifiPage({
  device,
  wifi,
  onToast,
}: {
  device: DeviceInfo
  wifi: WifiStatus
  onToast: (msg: string) => void
}) {
  const navigate = useNavigate()
  const [ssid, setSsid] = useState(wifi.connected ? wifi.ssid : '')
  const [password, setPassword] = useState('')
  const [aps, setAps] = useState<WifiScanAp[]>([])
  const [scanning, setScanning] = useState(false)
  const [busy, setBusy] = useState(false)

  const scan = useCallback(async () => {
    setScanning(true)
    try {
      for (let i = 0; i < 8; i++) {
        const result = await api.scanWifi()
        if (result !== 'pending') {
          setAps(result)
          break
        }
        await new Promise((r) => setTimeout(r, 500))
      }
    } catch {
      onToast('WLAN-Scan fehlgeschlagen')
    } finally {
      setScanning(false)
    }
  }, [onToast])

  useEffect(() => {
    void scan()
  }, [scan])

  async function connect(e: React.FormEvent) {
    e.preventDefault()
    setBusy(true)
    try {
      const res = await api.connectWifi(ssid, password)
      if (!res.ok) {
        onToast('Verbindung fehlgeschlagen')
        return
      }
      if (res.next === '/wifi-testing' || device.mode === 'ap') {
        navigate('/wifi-testing')
        return
      }
      onToast(res.message === 'saved_rebooting' ? 'Gespeichert — Gerät startet neu' : 'Gespeichert')
    } catch {
      onToast('Verbindung fehlgeschlagen')
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="space-y-4">
      <Panel title="Status">
        <div className="flex items-center justify-between gap-3">
          <StatusBadge ok={wifi.connected} />
          {wifi.connected ? (
            <div className="text-right text-sm text-muted">
              <div className="font-medium text-text-bright">{wifi.ssid}</div>
              <div>
                {wifi.ip} · {wifi.rssi} dBm
              </div>
            </div>
          ) : (
            <span className="text-sm text-muted">Kein Link</span>
          )}
        </div>
      </Panel>

      <Panel
        title="Netzwerke"
        action={
          <button
            type="button"
            onClick={() => void scan()}
            className="inline-flex items-center gap-1 text-xs font-semibold text-accent"
          >
            <RefreshCw size={14} className={scanning ? 'animate-spin' : ''} />
            Scan
          </button>
        }
      >
        <div className="space-y-2">
          {aps.length === 0 ? (
            <p className="text-sm text-muted">{scanning ? 'Suche…' : 'Keine Netze gefunden'}</p>
          ) : (
            aps.map((ap) => (
              <button
                key={`${ap.ssid}-${ap.rssi}`}
                type="button"
                onClick={() => setSsid(ap.ssid)}
                className="flex w-full items-center justify-between rounded-lg border border-border bg-bg px-3 py-2.5 text-left hover:border-accent/40"
              >
                <span className="inline-flex items-center gap-2 text-sm text-text-bright">
                  <Wifi size={16} className="text-accent" />
                  {ap.ssid || '(versteckt)'}
                  {!ap.open ? <Lock size={12} className="text-muted" /> : null}
                </span>
                <span className="text-xs text-muted">{ap.rssi} dBm</span>
              </button>
            ))
          )}
        </div>
      </Panel>

      <Panel title="Verbinden">
        <form className="space-y-3" onSubmit={(e) => void connect(e)}>
          <Field label="SSID">
            <TextInput value={ssid} onChange={(e) => setSsid(e.target.value)} required maxLength={32} />
          </Field>
          <Field label="Passwort" hint="Leer lassen für offene Netze">
            <TextInput
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              maxLength={64}
              autoComplete="current-password"
            />
          </Field>
          <PrimaryButton type="submit" disabled={busy || !ssid}>
            {device.mode === 'ap' ? 'Testen & verbinden' : 'Speichern & neu starten'}
          </PrimaryButton>
        </form>
      </Panel>
    </div>
  )
}
