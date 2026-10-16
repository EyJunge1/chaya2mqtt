# UI-Farben (Chaya2MQTT)

Kleine **semantische** Palette: Neutrals + Accent + Status (Light/Dark).  
Keine Extra-Hues pro Feature — Komponenten nutzen Tokens, nicht eigene Hex-Werte.

Single source of truth für die Web-UI: CSS-Variablen `--ui-*` in

- [`frontend/src/index.css`](../frontend/src/index.css) (Admin)
- [`flasher/src/index.css`](../flasher/src/index.css) (Web-Flasher — gleiche Palette)

Theme-Umschaltung: `data-theme="light"|"dark"` auf `<html>` (siehe `frontend/src/theme/`).

Tailwind nutzt die Aliase `--color-*` aus dem `@theme`-Block (`bg-bg`, `text-accent`, `border-border`, …).

## Tokens

| Token | Light | Dark | Verwendung |
|-------|-------|------|------------|
| `--ui-bg` | `#faf6f8` | `#0a0a0a` | Seitenhintergrund, `body`, Inputs |
| `--ui-surface` | `#ffffff` | `#141414` | Karten, Sidebar, Dialoge, Badges |
| `--ui-surface-hover` | `#f4e8ef` | `#1a1a1a` | Hover auf Nav / Zeilen / Buttons |
| `--ui-border` | `#e5d0db` | `#2a2a2a` | Rahmen, Trenner |
| `--ui-text` | `#4a3040` | `#fbe0ef` | Fließtext |
| `--ui-text-bright` | `#1a0f16` | `#ffffff` | Überschriften, betonte Werte |
| `--ui-accent` | `#e85a9b` | `#ef7ab4` | Primärfarbe, aktive Nav, CTAs |
| `--ui-danger` | `#d32f2f` | `#f44336` | Fehler, destruktive Aktionen |
| `--ui-warning` | `#c79100` | `#f0b429` | Warnungen, Reconnect-Hinweise |
| `--ui-status-ok` | `#059669` | `#34d399` | OK / online |
| `--ui-muted` | `#8a6a7a` | `#b08a9e` | Sekundärtext, inaktive Nav |
| `--ui-ring` | `#e85a9b` | `#ef7ab4` | Focus-Ring (`focus-ring`) |

Shadows (kein Light/Dark-Split): `--shadow-elevated`, `--shadow-dialog` im jeweiligen `@theme`-Block.

## Regeln

1. **Keine neuen Hardcoded-Hex** in Komponenten — immer Token / Tailwind-Alias.
2. Hex-Werte nur in den beiden `index.css`-Dateien ändern; Admin und Flasher synchron halten.
3. E-Ink-Display-Farben gehören **nicht** hierher (siehe Preview-Skript / Firmware).

## Bekannte Ausnahmen

- Batterie-Anzeigen auf dem Dashboard nutzen teils `color-mix` mit festen Hex-Werten (`#f59e0b`, `#22c55e`, `#e53935`) — langfristig an Status-Tokens anbinden.
