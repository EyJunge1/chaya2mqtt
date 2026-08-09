import { ArrowLeft, Heart } from 'lucide-react'
import type { ReactNode } from 'react'
import { Link, useLocation } from 'react-router-dom'
import type { DeviceInfo } from '../api/types'
import { StatusBadge } from './StatusBadge'

const titles: Record<string, string> = {
  '/': 'Chaya2MQTT',
  '/wifi': 'WLAN',
  '/wifi-testing': 'WLAN-Test',
  '/mqtt': 'MQTT',
  '/pairing': 'Pairing',
  '/settings': 'Einstellungen',
  '/update': 'Update',
}

export function Layout({
  device,
  children,
  wifiOk,
  mqttOk,
}: {
  device: DeviceInfo | null
  children: ReactNode
  wifiOk?: boolean
  mqttOk?: boolean
}) {
  const { pathname } = useLocation()
  const title = titles[pathname] ?? 'Chaya2MQTT'
  const showBack = pathname !== '/'

  return (
    <div className="mx-auto min-h-screen w-full max-w-[560px] px-4 py-5">
      <header className="mb-5">
        <div className="mb-3 flex items-center gap-3">
          {showBack ? (
            <Link
              to="/"
              className="inline-flex h-9 w-9 items-center justify-center rounded-lg border border-border bg-surface text-text-bright hover:bg-surface-hover"
              aria-label="Zurück"
            >
              <ArrowLeft size={18} />
            </Link>
          ) : (
            <span className="inline-flex h-9 w-9 items-center justify-center rounded-lg bg-accent/15 text-accent">
              <Heart size={18} fill="currentColor" />
            </span>
          )}
          <div className="min-w-0 flex-1">
            <h1 className="truncate text-xl font-bold text-text-bright">{title}</h1>
            {device ? (
              <p className="truncate text-xs text-muted">
                {device.hostname} · {device.version} · {device.mode.toUpperCase()}
                {device.deviceId ? ` · ${device.deviceId}` : ''}
              </p>
            ) : null}
          </div>
        </div>
        {pathname === '/' && (
          <div className="flex flex-wrap gap-2">
            <StatusBadge ok={Boolean(wifiOk)} labelOk="WLAN" labelBad="WLAN" />
            <StatusBadge ok={Boolean(mqttOk)} labelOk="MQTT" labelBad="MQTT" />
          </div>
        )}
      </header>
      <main className="space-y-4">{children}</main>
    </div>
  )
}
