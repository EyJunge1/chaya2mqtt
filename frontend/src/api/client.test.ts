import { beforeEach, describe, expect, it, vi } from 'vitest'
import { api, refreshCsrf, setCsrfToken } from './client'

describe('api client', () => {
  beforeEach(() => {
    setCsrfToken('abc123')
    vi.restoreAllMocks()
  })

  it('refreshCsrf stores token', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: true,
        status: 200,
        text: async () => JSON.stringify({ token: 'deadbeef' }),
      }),
    )
    await expect(refreshCsrf()).resolves.toBe('deadbeef')
  })

  it('sendChaya posts csrf form body', async () => {
    const fetchMock = vi.fn().mockResolvedValue({
      ok: true,
      status: 202,
      text: async () => JSON.stringify({ ok: true, queued: true }),
    })
    vi.stubGlobal('fetch', fetchMock)
    const res = await api.sendChaya()
    expect(res).toEqual({ ok: true, queued: true })
    expect(fetchMock).toHaveBeenCalledWith(
      '/api/chaya/send',
      expect.objectContaining({
        method: 'POST',
        body: 'csrf_token=abc123',
      }),
    )
  })

  it('scanWifi returns pending on 202', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn().mockResolvedValue({
        ok: false,
        status: 202,
        text: async () => '',
      }),
    )
    await expect(api.scanWifi()).resolves.toBe('pending')
  })
})
