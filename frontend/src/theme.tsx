import {
  createContext,
  createElement,
  useContext,
  useMemo,
  useSyncExternalStore,
  type ReactNode,
} from 'react'

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

function subscribe(listener: () => void) {
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

type ThemeContextValue = {
  theme: Theme
  setTheme: (theme: Theme) => void
}

const ThemeContext = createContext<ThemeContextValue | null>(null)

function useThemeStore(): Theme {
  return useSyncExternalStore(subscribe, getTheme, () => 'light' as Theme)
}

export function ThemeProvider({ children }: { children: ReactNode }) {
  const theme = useThemeStore()
  const value = useMemo<ThemeContextValue>(
    () => ({
      theme,
      setTheme,
    }),
    [theme],
  )
  return createElement(ThemeContext.Provider, { value }, children)
}

export function useTheme(): ThemeContextValue {
  const ctx = useContext(ThemeContext)
  const theme = useThemeStore()
  return useMemo(
    () =>
      ctx ?? {
        theme,
        setTheme,
      },
    [ctx, theme],
  )
}

if (typeof document !== 'undefined') {
  document.documentElement.dataset.theme = currentTheme
  document.documentElement.style.colorScheme = currentTheme
}
