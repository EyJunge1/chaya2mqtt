export type Theme = 'dark' | 'light'

let currentTheme: Theme = 'light'
const listeners = new Set<() => void>()

function emit() {
  for (const listener of listeners) listener()
}

function applyTheme(theme: Theme) {
  currentTheme = theme
  if (typeof document !== 'undefined') {
    document.documentElement.dataset.theme = theme
    document.documentElement.style.colorScheme = theme
  }
  emit()
}

export function subscribeTheme(listener: () => void) {
  listeners.add(listener)
  return () => {
    listeners.delete(listener)
  }
}

export function getTheme(): Theme {
  return currentTheme
}

export function setTheme(theme: Theme) {
  if (theme !== 'dark' && theme !== 'light') return
  applyTheme(theme)
}

if (typeof document !== 'undefined') {
  document.documentElement.dataset.theme = currentTheme
  document.documentElement.style.colorScheme = currentTheme
}
