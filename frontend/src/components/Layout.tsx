import { ArrowLeft, Heart } from 'lucide-react'
import type { ReactNode } from 'react'
import { Link, useLocation } from 'react-router-dom'
import type { DeviceMode } from '../api/types'
import { useI18n } from '../i18n/useI18n'
import type { TranslationKey } from '../i18n/translations'
import { StatusBadge } from './StatusBadge'

const titleKeys: Record<string, TranslationKey> = {
  '/': 'nav.dashboard',
  '/wifi': 'nav.wifi',
  '/wifi-testing': 'nav.wifi-testing',
  '/mqtt': 'nav.mqtt',
  '/pairing': 'nav.pairing',
  '/settings': 'nav.settings',
  '/update': 'nav.update',
}

export function Layout({
  children,
  mode = 'sta',
  wifiOk,
  mqttOk,
}: {
  children: ReactNode
  mode?: DeviceMode
  wifiOk?: boolean
  mqttOk?: boolean
}) {
  const { pathname } = useLocation()
  const { t } = useI18n()
  const title = t(titleKeys[pathname] ?? 'nav.dashboard')
  const showBack = pathname !== '/'
  const showStatus = pathname === '/' && mode === 'sta'

  return (
    <div className="mx-auto min-h-screen w-full max-w-140 px-4 pt-[max(1.25rem,env(safe-area-inset-top))] pb-[max(1.25rem,env(safe-area-inset-bottom))]">
      <header className="mb-5">
        <div className={`flex items-center gap-3 ${showStatus ? 'mb-3' : ''}`}>
          {showBack ? (
            <Link
              to="/"
              className="inline-flex size-11 shrink-0 items-center justify-center rounded-lg border border-border bg-surface text-text-bright transition hover:bg-surface-hover focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 focus-visible:ring-offset-bg"
              aria-label={t('nav.back')}
            >
              <ArrowLeft size={18} />
            </Link>
          ) : (
            <span className="inline-flex size-11 shrink-0 items-center justify-center rounded-lg bg-accent/15 text-accent">
              <Heart size={18} fill="currentColor" />
            </span>
          )}
          <h1 className="flex h-11 min-w-0 flex-1 items-center truncate text-[1.75rem] leading-none font-bold text-text-bright">
            {title}
          </h1>
        </div>
        {showStatus ? (
          <div className="flex flex-wrap gap-2">
            <StatusBadge
              ok={Boolean(wifiOk)}
              label={t('status.wifi')}
              detailOk={t('status.wifi-ok')}
              detailBad={t('status.wifi-bad')}
            />
            <StatusBadge
              ok={Boolean(mqttOk)}
              label={t('status.mqtt')}
              detailOk={t('status.mqtt-ok')}
              detailBad={t('status.mqtt-bad')}
            />
          </div>
        ) : null}
      </header>
      <main className="space-y-4">{children}</main>
    </div>
  )
}
