import { QRCodeSVG } from 'qrcode.react'
import { useEffect, useState } from 'react'
import { api } from '../api/client'
import type { PairingInfo } from '../api/types'
import { Panel } from '../components/Card'
import { Field, PrimaryButton, TextInput } from '../components/Form'

export function PairingPage({ onToast }: { onToast: (msg: string) => void }) {
  const [info, setInfo] = useState<PairingInfo | null>(null)
  const [partner, setPartner] = useState('')
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    void api
      .getPairing()
      .then((p) => {
        setInfo(p)
        setPartner(p.partnerId)
      })
      .catch(() => onToast('Pairing laden fehlgeschlagen'))
  }, [onToast])

  async function save(e: React.FormEvent) {
    e.preventDefault()
    setBusy(true)
    try {
      const res = await api.savePartner(partner.trim().toLowerCase())
      if (!res.ok) {
        onToast('Partner-ID ungültig')
        return
      }
      onToast('Partner gespeichert')
      const next = await api.getPairing()
      setInfo(next)
    } catch {
      onToast('Speichern fehlgeschlagen')
    } finally {
      setBusy(false)
    }
  }

  if (!info) {
    return <p className="text-sm text-muted">Lade Pairing…</p>
  }

  return (
    <div className="space-y-4">
      <Panel title="Diese Device-ID">
        <div className="flex flex-col items-center gap-3 py-2">
          <div className="rounded-xl bg-white p-3">
            <QRCodeSVG value={info.deviceId} size={160} level="M" />
          </div>
          <code className="text-lg font-bold tracking-widest text-text-bright">{info.deviceId}</code>
        </div>
      </Panel>
      <Panel title="Partner">
        <form className="space-y-3" onSubmit={(e) => void save(e)}>
          <Field label="Partner-ID (6 Hex)" hint="z. B. f5e6d7">
            <TextInput
              value={partner}
              onChange={(e) => setPartner(e.target.value)}
              maxLength={6}
              pattern="[0-9a-fA-F]{6}"
              required
            />
          </Field>
          <p className="text-xs text-muted">
            Pub: {info.topicPub || '—'}
            <br />
            Sub: {info.topicSub || '—'}
          </p>
          <PrimaryButton type="submit" disabled={busy}>
            Partner speichern
          </PrimaryButton>
        </form>
      </Panel>
    </div>
  )
}
