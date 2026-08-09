import { useCallback, useEffect, useState } from 'react'
import { Navigate, Route, Routes } from 'react-router-dom'
import { api, refreshCsrf } from './api/client'
import { connectEvents } from './api/sse'
import type { ChayaStatus, DeviceInfo, MqttStatus, WifiStatus } from './api/types'
import { Layout } from './components/Layout'
import { MockToolbar } from './components/MockToolbar'
import { Toast } from './components/Toast'
import { DashboardPage } from './pages/DashboardPage'
import { MqttPage } from './pages/MqttPage'
import { PairingPage } from './pages/PairingPage'
import { SettingsPage } from './pages/SettingsPage'
import { UpdatePage } from './pages/UpdatePage'
import { WifiPage } from './pages/WifiPage'
import { WifiTestingPage } from './pages/WifiTestingPage'

export default function App() {
  const [device, setDevice] = useState<DeviceInfo | null>(null)
  const [chaya, setChaya] = useState<ChayaStatus>({ rx: 0, tx: 0, connected: false })
  const [wifi, setWifi] = useState<WifiStatus>({ connected: false })
  const [mqtt, setMqtt] = useState<MqttStatus>({ connected: false })
  const [toast, setToast] = useState<string | null>(null)
  const [bootError, setBootError] = useState<string | null>(null)

  const refreshDevice = useCallback(async () => {
    await refreshCsrf()
    const d = await api.getDevice()
    setDevice(d)
    const [c, w, m] = await Promise.all([
      d.mode === 'sta' ? api.getChaya() : Promise.resolve({ rx: 0, tx: 0, connected: false }),
      api.getWifiStatus(),
      d.mode === 'sta' ? api.getMqttStatus() : Promise.resolve({ connected: false }),
    ])
    setChaya(c)
    setWifi(w)
    setMqtt(m)
  }, [])

  useEffect(() => {
    void refreshDevice().catch(() => setBootError('Gerät nicht erreichbar'))
  }, [refreshDevice])

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
      <div className="mx-auto max-w-[560px] px-4 py-10 text-center">
        <h1 className="mb-2 text-xl font-bold text-text-bright">Chaya2MQTT</h1>
        <p className="text-sm text-danger">{bootError}</p>
      </div>
    )
  }

  if (!device) {
    return (
      <div className="mx-auto max-w-[560px] px-4 py-10 text-center text-sm text-muted">
        Verbinde…
      </div>
    )
  }

  return (
    <>
      <MockToolbar onChanged={refreshDevice} />
      <Layout
        device={device}
        wifiOk={wifi.connected}
        mqttOk={mqtt.connected}
      >
        <Routes>
          <Route
            path="/"
            element={<DashboardPage device={device} chaya={chaya} onToast={setToast} />}
          />
          <Route
            path="/wifi"
            element={<WifiPage device={device} wifi={wifi} onToast={setToast} />}
          />
          <Route path="/wifi-testing" element={<WifiTestingPage onToast={setToast} />} />
          <Route path="/mqtt" element={<MqttPage mqtt={mqtt} onToast={setToast} />} />
          <Route path="/pairing" element={<PairingPage onToast={setToast} />} />
          <Route
            path="/settings"
            element={<SettingsPage onToast={setToast} onDeviceRefresh={refreshDevice} />}
          />
          <Route path="/update" element={<UpdatePage onToast={setToast} />} />
          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </Layout>
      <Toast message={toast} onClose={() => setToast(null)} />
    </>
  )
}
