import { useCallback, useEffect, useState } from 'react'
import { Navigate, Route, Routes } from 'react-router-dom'
import { api, refreshCsrf } from './api/client'
import { connectEvents } from './api/sse'
import type { ChayaStatus, DeviceInfo, MqttStatus, WifiStatus } from './api/types'
import { Layout } from './components/Layout'
import { MockToolbar } from './components/MockToolbar'
import { ErrorBlock, LoadingBlock } from './components/StateBlock'
import { Toast, type ShowToast, type ToastMessage, type ToastVariant } from './components/Toast'
import { useI18n } from './i18n'
import type { Lang } from './i18n/translations'
import { DashboardPage } from './pages/DashboardPage'
import { MqttPage } from './pages/MqttPage'
import { PairingPage } from './pages/PairingPage'
import { SettingsPage } from './pages/SettingsPage'
import { UpdatePage } from './pages/UpdatePage'
import { WifiPage } from './pages/WifiPage'
import { WifiTestingPage } from './pages/WifiTestingPage'
import { setTheme, type Theme } from './theme'

export default function App() {
  const { t, setLanguage } = useI18n()
  const [device, setDevice] = useState<DeviceInfo | null>(null)
  const [chaya, setChaya] = useState<ChayaStatus>({ rx: 0, tx: 0, connected: false })
  const [wifi, setWifi] = useState<WifiStatus>({ connected: false })
  const [mqtt, setMqtt] = useState<MqttStatus>({ connected: false })
  const [toast, setToast] = useState<ToastMessage>(null)
  const [bootError, setBootError] = useState(false)
  const [booting, setBooting] = useState(true)

  const showToast: ShowToast = useCallback((text, variant: ToastVariant = 'success') => {
    setToast({ text, variant })
  }, [])

  const refreshDevice = useCallback(async () => {
    await refreshCsrf()
    const d = await api.getDevice()
    setDevice(d)
    const [c, w, m, settings] = await Promise.all([
      d.mode === 'sta' ? api.getChaya() : Promise.resolve({ rx: 0, tx: 0, connected: false }),
      api.getWifiStatus(),
      d.mode === 'sta' ? api.getMqttStatus() : Promise.resolve({ connected: false }),
      api.getSettings().catch(() => null),
    ])
    setChaya(c)
    setWifi(w)
    setMqtt(m)
    if (settings) {
      if (settings.lang === 'de' || settings.lang === 'en') {
        setLanguage(settings.lang as Lang)
      }
      if (settings.theme === 'dark' || settings.theme === 'light') {
        setTheme(settings.theme as Theme)
      }
    }
  }, [setLanguage])

  const boot = useCallback(async () => {
    setBooting(true)
    setBootError(false)
    try {
      await refreshDevice()
    } catch {
      setBootError(true)
      setDevice(null)
    } finally {
      setBooting(false)
    }
  }, [refreshDevice])

  useEffect(() => {
    void boot()
  }, [boot])

  useEffect(() => {
    if (!device) return
    return connectEvents({
      chaya: setChaya,
      wifi: setWifi,
      mqtt: setMqtt,
    })
  }, [device])

  if (bootError) {
    return (
      <div className="mx-auto max-w-[560px] px-4 py-10">
        <h1 className="mb-4 text-center text-xl font-bold text-text-bright">{t('app.title')}</h1>
        <ErrorBlock
          title={t('app.boot-error-title')}
          message={t('app.boot-error')}
          retryLabel={t('app.retry')}
          onRetry={() => void boot()}
        />
      </div>
    )
  }

  if (booting || !device) {
    return (
      <div className="mx-auto max-w-[560px] px-4 py-10">
        <LoadingBlock label={t('app.connecting')} />
      </div>
    )
  }

  return (
    <>
      <MockToolbar onChanged={refreshDevice} mode={device.mode} />
      <Layout mode={device.mode} wifiOk={wifi.connected} mqttOk={mqtt.connected}>
        <Routes>
          <Route
            path="/"
            element={
              <DashboardPage device={device} chaya={chaya} wifi={wifi} onToast={showToast} />
            }
          />
          <Route
            path="/wifi"
            element={<WifiPage device={device} wifi={wifi} onToast={showToast} />}
          />
          <Route path="/wifi-testing" element={<WifiTestingPage onToast={showToast} />} />
          <Route path="/mqtt" element={<MqttPage mqtt={mqtt} onToast={showToast} />} />
          <Route path="/pairing" element={<PairingPage onToast={showToast} />} />
          <Route
            path="/settings"
            element={<SettingsPage onToast={showToast} onDeviceRefresh={refreshDevice} />}
          />
          <Route path="/update" element={<UpdatePage onToast={showToast} />} />
          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </Layout>
      <Toast message={toast} onClose={() => setToast(null)} />
    </>
  )
}
