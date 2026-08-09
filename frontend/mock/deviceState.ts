import { randomBytes } from 'node:crypto'

export type MockMode = 'ap' | 'sta'

export type MockScenario = 'sta-connected' | 'ap-setup' | 'offline'

export interface MockState {
  scenario: MockScenario
  mode: MockMode
  version: string
  hostname: string
  deviceId: string
  csrf: string
  rx: number
  tx: number
  mqttConnected: boolean
  wifiConnected: boolean
  wifiSsid: string
  wifiIp: string
  wifiRssi: number
  mqtt: {
    server: string
    port: number
    username: string
    password: string
    topicPub: string
    topicSub: string
    partnerId: string
  }
  resetDays: number
  lang: 'de' | 'en'
  theme: 'dark' | 'light'
  wifiConnect: {
    state: 'idle' | 'testing' | 'ok' | 'fail'
    ssid: string
    password: string
    startedAt: number
  }
  scanReadyAt: number
}

const listeners = new Set<(event: string, data: unknown) => void>()

function newToken(): string {
  return randomBytes(16).toString('hex')
}

export function createInitialState(scenario: MockScenario = 'sta-connected'): MockState {
  const state: MockState = {
    scenario,
    mode: 'sta',
    version: 'dev-sim',
    hostname: 'chaya2mqtt',
    deviceId: 'a1b2c3',
    csrf: newToken(),
    rx: 3,
    tx: 7,
    mqttConnected: true,
    wifiConnected: true,
    wifiSsid: 'MockNet',
    wifiIp: '192.168.1.42',
    wifiRssi: -55,
    mqtt: {
      server: 'mqtt.example.com',
      port: 8883,
      username: 'chaya',
      password: 'secret',
      topicPub: 'chaya/a1b2c3',
      topicSub: 'chaya/f5e6d7',
      partnerId: 'f5e6d7',
    },
    resetDays: 7,
    lang: 'en',
    theme: 'light',
    wifiConnect: { state: 'idle', ssid: '', password: '', startedAt: 0 },
    scanReadyAt: 0,
  }
  applyScenario(state, scenario)
  return state
}

export function applyScenario(state: MockState, scenario: MockScenario): void {
  state.scenario = scenario
  switch (scenario) {
    case 'ap-setup':
      state.mode = 'ap'
      state.wifiConnected = false
      state.mqttConnected = false
      state.wifiSsid = ''
      state.wifiIp = ''
      break
    case 'offline':
      state.mode = 'sta'
      state.wifiConnected = false
      state.mqttConnected = false
      state.wifiSsid = ''
      state.wifiIp = ''
      break
    case 'sta-connected':
    default:
      state.mode = 'sta'
      state.wifiConnected = true
      state.mqttConnected = true
      state.wifiSsid = state.wifiSsid || 'MockNet'
      state.wifiIp = state.wifiIp || '192.168.1.42'
      break
  }
  state.csrf = newToken()
}

let state = createInitialState('sta-connected')

export function getState(): MockState {
  return state
}

export function resetState(scenario?: MockScenario): MockState {
  state = createInitialState(scenario ?? state.scenario)
  broadcastAll()
  return state
}

export function subscribe(fn: (event: string, data: unknown) => void): () => void {
  listeners.add(fn)
  return () => listeners.delete(fn)
}

export function emit(event: string, data: unknown): void {
  for (const fn of listeners) fn(event, data)
}

export function chayaPayload() {
  return { rx: state.rx, tx: state.tx, connected: state.mqttConnected }
}

export function wifiPayload() {
  if (!state.wifiConnected) return { connected: false as const }
  return {
    connected: true as const,
    ssid: state.wifiSsid,
    ip: state.wifiIp,
    rssi: state.wifiRssi,
  }
}

export function mqttPayload() {
  return { connected: state.mqttConnected }
}

export function broadcastAll(): void {
  emit('chaya', chayaPayload())
  emit('wifi', wifiPayload())
  emit('mqtt', mqttPayload())
}

export function tickWifiConnect(): void {
  if (state.wifiConnect.state !== 'testing') return
  if (Date.now() - state.wifiConnect.startedAt < 2500) return
  const ok = state.wifiConnect.password !== 'fail'
  state.wifiConnect.state = ok ? 'ok' : 'fail'
  if (ok) {
    state.wifiSsid = state.wifiConnect.ssid
    state.wifiIp = '192.168.1.77'
    state.wifiRssi = -48
  }
}

export function devicePayload() {
  return {
    hostname: state.hostname,
    version: state.version,
    mode: state.mode,
    deviceId: state.deviceId,
  }
}
