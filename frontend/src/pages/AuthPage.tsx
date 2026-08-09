import { useState } from 'react'
import { useNavigate, useSearchParams } from 'react-router-dom'
import { api } from '../api/client'
import { Panel } from '../components/Card'
import { Field, PrimaryButton, TextInput } from '../components/Form'

export function AuthPage({
  onToast,
  onDeviceRefresh,
}: {
  onToast: (msg: string) => void
  onDeviceRefresh: () => Promise<void>
}) {
  const [code, setCode] = useState('')
  const [busy, setBusy] = useState(false)
  const [params] = useSearchParams()
  const navigate = useNavigate()
  const next = params.get('next') || '/'

  async function login(e: React.FormEvent) {
    e.preventDefault()
    setBusy(true)
    try {
      const res = await api.login(code, next)
      if (!res.ok) {
        if (res.error === 'lockout') {
          onToast(`Sperre aktiv (~${res.lockoutSec ?? '?'} s)`)
        } else {
          onToast('Code ungültig')
        }
        return
      }
      await onDeviceRefresh()
      navigate(res.next || next)
    } catch {
      onToast('Anmeldung fehlgeschlagen')
    } finally {
      setBusy(false)
    }
  }

  return (
    <Panel title="Web-Auth">
      <p className="mb-4 text-sm text-muted">
        Tippe kurz die Taste am Gerät, lies den 6-stelligen Code auf dem Display und gib ihn hier
        ein. Im Simulator gilt der Code <code className="text-accent">123456</code>.
      </p>
      <form className="space-y-3" onSubmit={(e) => void login(e)}>
        <Field label="Code">
          <TextInput
            value={code}
            onChange={(e) => setCode(e.target.value.replace(/\D/g, '').slice(0, 6))}
            inputMode="numeric"
            pattern="\d{6}"
            maxLength={6}
            required
            autoFocus
          />
        </Field>
        <PrimaryButton type="submit" disabled={busy || code.length !== 6}>
          Anmelden
        </PrimaryButton>
      </form>
    </Panel>
  )
}
