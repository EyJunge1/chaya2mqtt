import { Heart, Link2, Radio, Settings, Upload, Wifi } from 'lucide-react'
import { useState } from 'react'
import { api } from '../api/client'
import type { ChayaStatus, DeviceInfo } from '../api/types'
import { NavCard, Panel } from '../components/Card'
import { PrimaryButton } from '../components/Form'

export function DashboardPage({
  device,
  chaya,
  onToast,
}: {
  device: DeviceInfo
  chaya: ChayaStatus
  onToast: (msg: string) => void
}) {
  const [busy, setBusy] = useState(false)
  const isAp = device.mode === 'ap'

  async function sendHeart() {
    setBusy(true)
    try {
      const res = await api.sendChaya()
      if (!res.ok) {
        onToast('Senden fehlgeschlagen (MQTT offline?)')
      } else {
        onToast('Herz gesendet')
      }
    } catch {
      onToast('Senden fehlgeschlagen')
    } finally {
      setBusy(false)
    }
  }

  if (isAp) {
    return (
      <div className="space-y-4">
        <Panel title="Ersteinrichtung">
          <p className="text-sm text-muted">
            Verbinde das Gerät mit deinem WLAN, damit Chaya2MQTT im Heimnetz erreichbar wird.
          </p>
        </Panel>
        <NavCard to="/wifi" title="WLAN einrichten" subtitle="Netzwerk wählen" icon={Wifi} />
      </div>
    )
  }

  return (
    <div className="space-y-4">
      <Panel title="Herzen">
        <div className="mb-4 grid grid-cols-2 gap-3">
          <div className="rounded-xl bg-bg px-3 py-4 text-center">
            <div className="text-xs uppercase tracking-wide text-muted">Empfangen</div>
            <div className="mt-1 text-3xl font-bold text-text-bright">{chaya.rx}</div>
          </div>
          <div className="rounded-xl bg-bg px-3 py-4 text-center">
            <div className="text-xs uppercase tracking-wide text-muted">Gesendet</div>
            <div className="mt-1 text-3xl font-bold text-text-bright">{chaya.tx}</div>
          </div>
        </div>
        <PrimaryButton onClick={sendHeart} disabled={busy || !chaya.connected}>
          <span className="inline-flex items-center justify-center gap-2">
            <Heart size={18} fill="currentColor" />
            Herz senden
          </span>
        </PrimaryButton>
      </Panel>

      <div className="grid gap-3">
        <NavCard to="/wifi" title="WLAN" subtitle="Netzwerk & Status" icon={Wifi} />
        <NavCard to="/mqtt" title="MQTT" subtitle="Broker & Topics" icon={Radio} />
        <NavCard to="/pairing" title="Pairing" subtitle="Partner verbinden" icon={Link2} />
        <NavCard to="/settings" title="Einstellungen" subtitle="Auth & Reset" icon={Settings} />
        <NavCard to="/update" title="Update" subtitle="Firmware prüfen" icon={Upload} />
      </div>
    </div>
  )
}
