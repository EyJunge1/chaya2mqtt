import { cleanup, render, screen } from '@testing-library/react'
import { afterEach, describe, expect, it } from 'vitest'
import { PrimaryButton } from './Form'

afterEach(() => {
  cleanup()
})

describe('PrimaryButton', () => {
  it('exposes busy state while loading', () => {
    render(
      <PrimaryButton loading type="button">
        Speichern
      </PrimaryButton>,
    )
    const btn = screen.getByRole('button', { name: /Speichern/i })
    expect(btn).toBeDisabled()
    expect(btn).toHaveAttribute('aria-busy', 'true')
  })
})
