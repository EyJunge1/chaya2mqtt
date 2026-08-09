import { useEffect, useState } from 'react'
import { api } from '../api/client'
import type { SettingsInfo } from '../api/types'
import { Panel } from '../components/Card'
import { DangerButton, Field, PrimaryButton, TextInput } from '../components/Form'

export function SettingsPage({
  onToast,
  onDeviceRefresh,
}: {
  onToast: (msg: string) => void
  onDeviceRefresh: () => Promise<void>
}) {
  const [settings, setSettings] = useState<SettingsInfo | null>(null)
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    void api.getSettings().then(setSettings).catch(() => onToast('Einstellungen laden fehlgeschlagen'))
  }, [onToast])

  async function save(e: React.FormEvent) {
    e.preventDefault()
    if (!settings) return
    setBusy(true)
    try {
      const res = await api.saveSettings(settings.resetDays)
      onToast(res.ok ? 'Gespeichert' : 'Speichern fehlgeschlagen')
      await onDeviceRefresh()
    } catch {
      onToast('Speichern fehlgeschlagen')
    } finally {
      setBusy(false)
    }
  }

  async function reboot() {
    setBusy(true)
    try {
      const res = await api.reboot()
      onToast(res.ok ? 'Neustart…' : 'Neustart fehlgeschlagen')
    } catch {
      onToast('Neustart fehlgeschlagen')
    } finally {
      setBusy(false)
    }
  }

  if (!settings) {
    return <p className="text-sm text-muted">Lade Einstellungen…</p>
  }

  return (
    <div className="space-y-4">
      <Panel title="Allgemein">
        <form className="space-y-3" onSubmit={(e) => void save(e)}>
          <Field label="Reset-Periode (Tage)" hint="0 = aus, 1–30 Tage">
            <TextInput
              type="number"
              min={0}
              max={30}
              value={settings.resetDays}
              onChange={(e) =>
                setSettings({ ...settings, resetDays: Number(e.target.value) })
              }
            />
          </Field>
          <PrimaryButton type="submit" disabled={busy}>
            Speichern
          </PrimaryButton>
        </form>
      </Panel>

      <Panel title="Gerät">
        <DangerButton disabled={busy} onClick={() => void reboot()}>
          Gerät neu starten
        </DangerButton>
      </Panel>
    </div>
  )
}
