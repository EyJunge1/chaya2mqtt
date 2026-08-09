import type {
  ApiResult,
  ChayaStatus,
  DeviceInfo,
  MqttConfigView,
  MqttStatus,
  PairingInfo,
  SettingsInfo,
  WifiConnectStatus,
  WifiScanAp,
  WifiStatus,
} from './types'

let csrfToken = ''

export function getCsrfToken(): string {
  return csrfToken
}

export function setCsrfToken(token: string): void {
  csrfToken = token
}

async function parseJson<T>(res: Response): Promise<T> {
  const text = await res.text()
  if (!text) {
    throw new Error(`Empty response (${res.status})`)
  }
  return JSON.parse(text) as T
}

function formBody(fields: Record<string, string | number | boolean | undefined>): string {
  const body = new URLSearchParams()
  body.set('csrf_token', csrfToken)
  for (const [key, value] of Object.entries(fields)) {
    if (value === undefined) continue
    if (typeof value === 'boolean') {
      if (value) body.set(key, '1')
      continue
    }
    body.set(key, String(value))
  }
  return body.toString()
}

async function apiGet<T>(path: string): Promise<T> {
  const res = await fetch(path, { credentials: 'same-origin' })
  if (!res.ok) {
    throw new Error(`${path} failed (${res.status})`)
  }
  return parseJson<T>(res)
}

async function apiPost(path: string, fields: Record<string, string | number | boolean | undefined> = {}): Promise<ApiResult> {
  const res = await fetch(path, {
    method: 'POST',
    credentials: 'same-origin',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8' },
    body: formBody(fields),
  })
  const data = await parseJson<ApiResult>(res)
  if (!res.ok && data && typeof data === 'object' && 'ok' in data && data.ok === false) {
    return data
  }
  if (!res.ok) {
    return { ok: false, error: `request_failed_${res.status}` }
  }
  return data
}

export async function refreshCsrf(): Promise<string> {
  const data = await apiGet<{ token: string }>('/api/csrf')
  csrfToken = data.token
  return csrfToken
}

export const api = {
  getDevice: () => apiGet<DeviceInfo>('/api/device'),
  getChaya: () => apiGet<ChayaStatus>('/api/chaya'),
  sendChaya: () => apiPost('/api/chaya/send'),
  getWifiStatus: () => apiGet<WifiStatus>('/api/wifi/status'),
  scanWifi: async (): Promise<WifiScanAp[] | 'pending'> => {
    const res = await fetch('/api/wifi/scan', { credentials: 'same-origin' })
    if (res.status === 202) return 'pending'
    if (!res.ok) throw new Error(`wifi scan failed (${res.status})`)
    return parseJson<WifiScanAp[]>(res)
  },
  connectWifi: (ssid: string, password: string) =>
    apiPost('/api/wifi/connect', { ssid, password }),
  getWifiConnectStatus: () => apiGet<WifiConnectStatus>('/api/wifi/connect-status'),
  commitWifiConnect: () => apiPost('/api/wifi/connect-commit'),
  abortWifiConnect: () => apiPost('/api/wifi/connect-abort'),
  getMqttStatus: () => apiGet<MqttStatus>('/api/mqtt/status'),
  getMqttConfig: () => apiGet<MqttConfigView>('/api/mqtt'),
  saveMqtt: (fields: {
    mqtt_server: string
    mqtt_port: number
    mqtt_user: string
    mqtt_pass?: string
    mqtt_topic_pub: string
    mqtt_topic_sub: string
  }) => apiPost('/api/mqtt', fields),
  getPairing: () => apiGet<PairingInfo>('/api/pairing'),
  savePartner: (partner_id: string) => apiPost('/api/pairing', { partner_id }),
  getSettings: () => apiGet<SettingsInfo>('/api/settings'),
  saveSettings: (fields: {
    reset_days: number
    lang: string
    theme: string
  }) => apiPost('/api/settings', fields),
  reboot: () => apiPost('/api/reboot'),
  checkUpdate: () => apiPost('/api/update/check'),
}
