import type { Connect, Plugin } from 'vite'
import type { IncomingMessage, ServerResponse } from 'node:http'
import {
  applyScenario,
  authRequired,
  broadcastAll,
  chayaPayload,
  devicePayload,
  getState,
  mqttPayload,
  resetState,
  rotateCsrf,
  subscribe,
  tickWifiConnect,
  wifiPayload,
  type MockScenario,
} from './deviceState.ts'

type Next = (err?: unknown) => void

function readBody(req: IncomingMessage): Promise<string> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = []
    req.on('data', (c) => chunks.push(Buffer.isBuffer(c) ? c : Buffer.from(c)))
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')))
    req.on('error', reject)
  })
}

function sendJson(res: ServerResponse, code: number, body: unknown): void {
  const payload = JSON.stringify(body)
  res.statusCode = code
  res.setHeader('Content-Type', 'application/json; charset=utf-8')
  res.setHeader('Cache-Control', 'no-store')
  res.end(payload)
}

function parseForm(body: string): URLSearchParams {
  return new URLSearchParams(body)
}

function requireCsrf(params: URLSearchParams, res: ServerResponse): boolean {
  if (params.get('csrf_token') !== getState().csrf) {
    sendJson(res, 403, { ok: false, error: 'csrf' })
    return false
  }
  return true
}

function pathOf(url: string): string {
  return url.split('?')[0] ?? url
}

async function handleApi(req: IncomingMessage, res: ServerResponse): Promise<boolean> {
  const url = req.url ?? '/'
  const path = pathOf(url)
  const method = (req.method ?? 'GET').toUpperCase()
  const state = getState()

  if (path === '/api/_mock/scenario' && method === 'POST') {
    const params = parseForm(await readBody(req))
    const scenario = (params.get('scenario') ?? 'sta-connected') as MockScenario
    applyScenario(state, scenario)
    broadcastAll()
    sendJson(res, 200, { ok: true, scenario: state.scenario, device: devicePayload() })
    return true
  }

  if (path === '/api/_mock/reset' && method === 'POST') {
    resetState('sta-connected')
    sendJson(res, 200, { ok: true, device: devicePayload() })
    return true
  }

  if (path === '/api/csrf' && method === 'GET') {
    sendJson(res, 200, { token: state.csrf })
    return true
  }

  if (path === '/api/device' && method === 'GET') {
    sendJson(res, 200, devicePayload())
    return true
  }

  const needsAuth =
    path.startsWith('/api/') &&
    path !== '/api/csrf' &&
    path !== '/api/device' &&
    path !== '/api/auth/login' &&
    !(state.mode === 'ap' && path.startsWith('/api/wifi'))

  if (needsAuth && authRequired()) {
    sendJson(res, 401, { ok: false, error: 'auth_required' })
    return true
  }

  if (path === '/api/chaya' && method === 'GET') {
    sendJson(res, 200, chayaPayload())
    return true
  }

  if (path === '/api/chaya/send' && method === 'POST') {
    const params = parseForm(await readBody(req))
    if (!requireCsrf(params, res)) return true
    if (!state.mqttConnected) {
      sendJson(res, 503, { ok: false, error: 'mqtt_offline' })
      return true
    }
    state.tx += 1
    broadcastAll()
    sendJson(res, 202, { ok: true, queued: true })
    return true
  }

  if (path === '/api/wifi/status' && method === 'GET') {
    sendJson(res, 200, wifiPayload())
    return true
  }

  if (path === '/api/wifi/scan' && method === 'GET') {
    if (state.scanReadyAt === 0) {
      state.scanReadyAt = Date.now() + 800
      sendJson(res, 202, null)
      return true
    }
    if (Date.now() < state.scanReadyAt) {
      sendJson(res, 202, null)
      return true
    }
    state.scanReadyAt = 0
    sendJson(res, 200, [
      { ssid: 'MockNet', rssi: -48, open: false },
      { ssid: 'CafeGuest', rssi: -67, open: true },
      { ssid: 'IoT-Lab', rssi: -72, open: false },
    ])
    return true
  }

  if (path === '/api/wifi/connect' && method === 'POST') {
    const params = parseForm(await readBody(req))
    if (!requireCsrf(params, res)) return true
    const ssid = params.get('ssid') ?? ''
    const password = params.get('password') ?? ''
    if (!ssid) {
      sendJson(res, 400, { ok: false, error: 'ssid' })
      return true
    }
    if (state.mode === 'ap') {
      state.wifiConnect = {
        state: 'testing',
        ssid,
        password,
        startedAt: Date.now(),
      }
      sendJson(res, 200, { ok: true, next: '/wifi-testing' })
      return true
    }
    state.wifiSsid = ssid
    state.wifiConnected = true
    state.wifiIp = '192.168.1.42'
    broadcastAll()
    sendJson(res, 200, { ok: true, message: 'saved_rebooting' })
    return true
  }

  if (path === '/api/wifi/connect-status' && method === 'GET') {
    tickWifiConnect()
    sendJson(res, 200, {
      state: state.wifiConnect.state,
      ssid: state.wifiConnect.ssid,
    })
    return true
  }

  if (path === '/api/wifi/connect-commit' && method === 'POST') {
    const params = parseForm(await readBody(req))
    if (!requireCsrf(params, res)) return true
    if (state.wifiConnect.state !== 'ok') {
      sendJson(res, 400, { ok: false, error: 'not_ok' })
      return true
    }
    state.mode = 'sta'
    state.wifiConnected = true
    state.wifiSsid = state.wifiConnect.ssid
    state.wifiConnect.state = 'idle'
    broadcastAll()
    sendJson(res, 200, { ok: true, message: 'committed' })
    return true
  }

  if (path === '/api/wifi/connect-abort' && method === 'POST') {
    const params = parseForm(await readBody(req))
    if (!requireCsrf(params, res)) return true
    state.wifiConnect = { state: 'idle', ssid: '', password: '', startedAt: 0 }
    sendJson(res, 200, { ok: true, next: '/wifi' })
    return true
  }

  if (path === '/api/mqtt/status' && method === 'GET') {
    sendJson(res, 200, mqttPayload())
    return true
  }

  if (path === '/api/mqtt' && method === 'GET') {
    sendJson(res, 200, {
      server: state.mqtt.server,
      port: state.mqtt.port,
      username: state.mqtt.username,
      hasPassword: state.mqtt.password.length > 0,
      topicPub: state.mqtt.topicPub,
      topicSub: state.mqtt.topicSub,
      partnerId: state.mqtt.partnerId,
    })
    return true
  }

  if (path === '/api/mqtt' && method === 'POST') {
    const params = parseForm(await readBody(req))
    if (!requireCsrf(params, res)) return true
    state.mqtt.server = params.get('mqtt_server') ?? ''
    state.mqtt.port = Number(params.get('mqtt_port') ?? '8883') || 8883
    state.mqtt.username = params.get('mqtt_user') ?? ''
    const pass = params.get('mqtt_pass')
    if (pass) state.mqtt.password = pass
    state.mqtt.topicPub = params.get('mqtt_topic_pub') ?? state.mqtt.topicPub
    state.mqtt.topicSub = params.get('mqtt_topic_sub') ?? state.mqtt.topicSub
    state.mqtt.partnerId = ''
    state.mqttConnected = Boolean(state.mqtt.server)
    broadcastAll()
    sendJson(res, 200, { ok: true, message: 'saved' })
    return true
  }

  if (path === '/api/pairing' && method === 'GET') {
    sendJson(res, 200, {
      deviceId: state.deviceId,
      partnerId: state.mqtt.partnerId,
      topicPub: state.mqtt.topicPub,
      topicSub: state.mqtt.topicSub,
    })
    return true
  }

  if (path === '/api/pairing' && method === 'POST') {
    const params = parseForm(await readBody(req))
    if (!requireCsrf(params, res)) return true
    const partner = (params.get('partner_id') ?? '').toLowerCase()
    if (!/^[0-9a-f]{6}$/.test(partner) || partner === state.deviceId) {
      sendJson(res, 400, { ok: false, error: 'partner' })
      return true
    }
    state.mqtt.partnerId = partner
    state.mqtt.topicPub = `chaya/${state.deviceId}`
    state.mqtt.topicSub = `chaya/${partner}`
    sendJson(res, 200, { ok: true, message: 'saved' })
    return true
  }

  if (path === '/api/settings' && method === 'GET') {
    sendJson(res, 200, {
      resetDays: state.resetDays,
      authEnabled: state.authEnabled,
    })
    return true
  }

  if (path === '/api/settings' && method === 'POST') {
    const params = parseForm(await readBody(req))
    if (!requireCsrf(params, res)) return true
    const days = Number(params.get('reset_days') ?? '7')
    state.resetDays = Number.isFinite(days) ? Math.min(30, Math.max(0, days)) : 7
    state.authEnabled = params.has('auth_enabled')
    if (!state.authEnabled) state.authenticated = true
    sendJson(res, 200, { ok: true, message: 'saved' })
    return true
  }

  if (path === '/api/auth/login' && method === 'POST') {
    const params = parseForm(await readBody(req))
    if (!requireCsrf(params, res)) return true
    if (!state.authEnabled) {
      sendJson(res, 200, { ok: true, next: '/' })
      return true
    }
    if (Date.now() < state.lockoutUntil) {
      sendJson(res, 429, {
        ok: false,
        error: 'lockout',
        lockoutSec: Math.ceil((state.lockoutUntil - Date.now()) / 1000),
      })
      return true
    }
    const code = params.get('code') ?? ''
    if (code !== state.authCode) {
      state.failStreak += 1
      if (state.failStreak >= 3) {
        state.lockoutUntil = Date.now() + 60_000
        state.failStreak = 0
      }
      sendJson(res, 401, { ok: false, error: 'bad_code' })
      return true
    }
    state.failStreak = 0
    state.authenticated = true
    state.sessionId = rotateCsrf()
    rotateCsrf()
    sendJson(res, 200, { ok: true, next: params.get('next') || '/' })
    return true
  }

  if (path === '/api/auth/logout' && method === 'POST') {
    const params = parseForm(await readBody(req))
    if (!requireCsrf(params, res)) return true
    state.authenticated = false
    state.sessionId = null
    rotateCsrf()
    sendJson(res, 200, { ok: true, next: '/auth' })
    return true
  }

  if (path === '/api/reboot' && method === 'POST') {
    const params = parseForm(await readBody(req))
    if (!requireCsrf(params, res)) return true
    sendJson(res, 200, { ok: true, message: 'rebooting' })
    return true
  }

  if (path === '/api/update/check' && method === 'POST') {
    const params = parseForm(await readBody(req))
    if (!requireCsrf(params, res)) return true
    sendJson(res, 200, { ok: true, message: 'checking' })
    return true
  }

  return false
}

function handleSse(req: IncomingMessage, res: ServerResponse): boolean {
  const path = pathOf(req.url ?? '/')
  if (path !== '/events') return false

  res.writeHead(200, {
    'Content-Type': 'text/event-stream',
    'Cache-Control': 'no-cache',
    Connection: 'keep-alive',
  })

  const write = (event: string, data: unknown) => {
    res.write(`event: ${event}\ndata: ${JSON.stringify(data)}\n\n`)
  }

  write('chaya', chayaPayload())
  write('wifi', wifiPayload())
  write('mqtt', mqttPayload())

  const unsub = subscribe((event: string, data: unknown) => write(event, data))
  const heartbeat = setInterval(() => res.write(': ping\n\n'), 15000)

  req.on('close', () => {
    clearInterval(heartbeat)
    unsub()
  })
  return true
}

function middleware(): Connect.NextHandleFunction {
  return async (req, res, next: Next) => {
    try {
      if (await handleApi(req, res)) return
      if (handleSse(req, res)) return
      next()
    } catch (err) {
      next(err)
    }
  }
}

export function mockDevicePlugin(): Plugin {
  return {
    name: 'chaya2mqtt-mock-device',
    configureServer(server) {
      server.middlewares.use(middleware())
    },
    configurePreviewServer(server) {
      server.middlewares.use(middleware())
    },
  }
}
