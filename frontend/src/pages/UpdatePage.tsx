import { useState } from 'react'
import { api } from '../api/client'
import { Panel } from '../components/Card'
import { PrimaryButton } from '../components/Form'

export function UpdatePage({ onToast }: { onToast: (msg: string) => void }) {
  const [busy, setBusy] = useState(false)

  async function check() {
    setBusy(true)
    try {
      const res = await api.checkUpdate()
      onToast(
        res.ok
          ? 'GitHub wird geprüft — das Gerät kann danach installieren'
          : 'Update-Check fehlgeschlagen',
      )
    } catch {
      onToast('Update-Check fehlgeschlagen')
    } finally {
      setBusy(false)
    }
  }

  return (
    <Panel title="Firmware-Update">
      <p className="mb-4 text-sm text-muted">
        Prüft auf GitHub, ob eine neuere Firmware verfügbar ist. Bei Erfolg kann das Gerät die
        Aktualisierung selbstständig installieren.
      </p>
      <PrimaryButton disabled={busy} onClick={() => void check()}>
        Nach Updates suchen
      </PrimaryButton>
    </Panel>
  )
}
