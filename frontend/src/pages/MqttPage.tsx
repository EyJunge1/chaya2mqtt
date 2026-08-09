import { useEffect, useState } from 'react'
import { api } from '../api/client'
import type { MqttConfigView, MqttStatus } from '../api/types'
import { Panel } from '../components/Card'
import { Field, PrimaryButton, TextInput } from '../components/Form'
import { StatusBadge } from '../components/StatusBadge'

export function MqttPage({
  mqtt,
  onToast,
}: {
  mqtt: MqttStatus
  onToast: (msg: string) => void
}) {
  const [cfg, setCfg] = useState<MqttConfigView | null>(null)
  const [password, setPassword] = useState('')
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    void api.getMqttConfig().then(setCfg).catch(() => onToast('MQTT-Config laden fehlgeschlagen'))
  }, [onToast])

  async function save(e: React.FormEvent) {
    e.preventDefault()
    if (!cfg) return
    setBusy(true)
    try {
      const res = await api.saveMqtt({
        mqtt_server: cfg.server,
        mqtt_port: cfg.port,
        mqtt_user: cfg.username,
        mqtt_pass: password || undefined,
        mqtt_topic_pub: cfg.topicPub,
        mqtt_topic_sub: cfg.topicSub,
      })
      onToast(res.ok ? 'Gespeichert. MQTT verbindet neu.' : 'Speichern fehlgeschlagen')
      if (res.ok) setPassword('')
    } catch {
      onToast('Speichern fehlgeschlagen')
    } finally {
      setBusy(false)
    }
  }

  if (!cfg) {
    return <p className="text-sm text-muted">Lade MQTT…</p>
  }

  return (
    <div className="space-y-4">
      <Panel title="Status">
        <StatusBadge ok={mqtt.connected} />
      </Panel>
      <Panel title="Broker">
        <form className="space-y-3" onSubmit={(e) => void save(e)}>
          <Field label="Broker (Hostname oder IP)">
            <TextInput
              value={cfg.server}
              onChange={(e) => setCfg({ ...cfg, server: e.target.value })}
              maxLength={127}
              required
            />
          </Field>
          <Field label="Port">
            <TextInput
              type="number"
              min={1}
              max={65535}
              value={cfg.port}
              onChange={(e) => setCfg({ ...cfg, port: Number(e.target.value) })}
              required
            />
          </Field>
          <Field label="Benutzername (optional)">
            <TextInput
              value={cfg.username}
              onChange={(e) => setCfg({ ...cfg, username: e.target.value })}
              maxLength={63}
            />
          </Field>
          <Field
            label="Passwort (optional)"
            hint={cfg.hasPassword ? 'Gespeichert — leer lassen zum Behalten' : undefined}
          >
            <TextInput
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              maxLength={63}
              autoComplete="current-password"
              placeholder={cfg.hasPassword ? '(gespeichert)' : ''}
            />
          </Field>
          <Field label="Topic Publish">
            <TextInput
              value={cfg.topicPub}
              onChange={(e) => setCfg({ ...cfg, topicPub: e.target.value })}
              maxLength={127}
              required
            />
          </Field>
          <Field label="Topic Subscribe">
            <TextInput
              value={cfg.topicSub}
              onChange={(e) => setCfg({ ...cfg, topicSub: e.target.value })}
              maxLength={127}
              required
            />
          </Field>
          <PrimaryButton type="submit" disabled={busy}>
            Speichern
          </PrimaryButton>
        </form>
      </Panel>
    </div>
  )
}
