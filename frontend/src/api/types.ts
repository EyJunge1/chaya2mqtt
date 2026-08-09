export type DeviceMode = 'ap' | 'sta'

export interface DeviceInfo {
  hostname: string
  version: string
  mode: DeviceMode
  deviceId: string
}

export interface ChayaStatus {
  rx: number
  tx: number
  connected: boolean
}

export interface WifiStatusDisconnected {
  connected: false
}

export interface WifiStatusConnected {
  connected: true
  ssid: string
  ip: string
  rssi: number
}

export type WifiStatus = WifiStatusDisconnected | WifiStatusConnected

export interface WifiScanAp {
  ssid: string
  rssi: number
  open: boolean
}

export type WifiConnectState = 'idle' | 'testing' | 'ok' | 'fail'

export interface WifiConnectStatus {
  state: WifiConnectState
  ssid: string
}

export interface MqttStatus {
  connected: boolean
}

export interface MqttConfigView {
  server: string
  port: number
  username: string
  hasPassword: boolean
  topicPub: string
  topicSub: string
  partnerId: string
}

export interface PairingInfo {
  deviceId: string
  partnerId: string
  topicPub: string
  topicSub: string
}

export interface SettingsInfo {
  resetDays: number
}

export interface ApiOk {
  ok: true
  message?: string
  queued?: boolean
  next?: string
}

export interface ApiErr {
  ok: false
  error: string
}

export type ApiResult = ApiOk | ApiErr
