import { QRCodeSVG } from 'qrcode.react'
import { useCallback, useEffect, useState } from 'react'
import type { ShowToast } from '../components/Toast'
import { api } from '../api/client'
import type { PairingInfo } from '../api/types'
import { Panel } from '../components/Card'
import { Field, PrimaryButton, TextInput } from '../components/Form'
import { ErrorBlock, LoadingBlock } from '../components/StateBlock'
import { useI18n } from '../i18n/useI18n'

export function PairingPage({ onToast }: { onToast: ShowToast }) {
  const { t } = useI18n()
  const [info, setInfo] = useState<PairingInfo | null>(null)
  const [partner, setPartner] = useState('')
  const [busy, setBusy] = useState(false)
  const [loadError, setLoadError] = useState(false)

  const load = useCallback(async () => {
    setLoadError(false)
    setInfo(null)
    try {
      const p = await api.getPairing()
      setInfo(p)
      setPartner(p.partnerId)
    } catch {
      setLoadError(true)
      onToast(t('toast.pairing-load-failed'), 'error')
    }
  }, [onToast, t])

  useEffect(() => {
    void load()
  }, [load])

  async function save(e: React.FormEvent) {
    e.preventDefault()
    setBusy(true)
    try {
      const res = await api.savePartner(partner.trim().toLowerCase())
      if (!res.ok) {
        onToast(t('toast.partner-invalid'), 'error')
        return
      }
      onToast(t('toast.partner-saved'), 'success')
      const next = await api.getPairing()
      setInfo(next)
    } catch {
      onToast(t('toast.save-failed'), 'error')
    } finally {
      setBusy(false)
    }
  }

  if (loadError) {
    return (
      <ErrorBlock
        title={t('pairing.load-error-title')}
        message={t('pairing.load-error')}
        retryLabel={t('common.retry')}
        onRetry={() => void load()}
      />
    )
  }

  if (!info) {
    return <LoadingBlock label={t('pairing.loading')} />
  }

  return (
    <div className="space-y-4">
      <Panel title={t('pairing.device-id')}>
        <div className="flex flex-col items-center gap-3 py-2">
          <div className="rounded-xl bg-white p-3">
            <QRCodeSVG value={info.deviceId} size={160} level="M" />
          </div>
          <code className="text-lg font-bold tracking-widest text-text-bright">
            {info.deviceId}
          </code>
        </div>
      </Panel>
      <Panel title={t('pairing.partner')}>
        <form className="space-y-3" onSubmit={(e) => void save(e)}>
          <Field label={t('pairing.partner-id')} hint={t('pairing.partner-hint')}>
            <TextInput
              value={partner}
              onChange={(e) => setPartner(e.target.value)}
              maxLength={6}
              pattern="[0-9a-fA-F]{6}"
              required
            />
          </Field>
          <PrimaryButton type="submit" loading={busy}>
            {t('common.save')}
          </PrimaryButton>
        </form>
      </Panel>
    </div>
  )
}
