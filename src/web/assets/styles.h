#pragma once

#include <Arduino.h>

/** Admin UI stylesheet. */
static const char WEB_COMMON_CSS[] PROGMEM = R"chaya2mqtt_css(<style>
:root {
    --bg: #0a0a0a;
    --surface: #141414;
    --surface-hover: #1a1a1a;
    --border: #2a2a2a;
    --text: #dff9e3;
    --text-bright: #fff;
    --accent: #1dae6b;
    --danger: #f44336;
    --danger-border: rgba(244, 67, 54, 0.15);
    --success: #76d39e;
}

* {
    box-sizing: border-box;
}

html {
    color-scheme: dark;
}

body {
    font-family: system-ui, sans-serif;
    margin: 0;
    padding: 16px;
    max-width: 560px;
    background: var(--bg);
    color: var(--text);
}

h1 {
    font-size: 1.8rem;
    margin: 0 0 16px;
    color: var(--text-bright);
    text-align: center;
}

label {
    display: block;
    margin: 12px 0 4px;
    font-weight: 600;
    color: var(--text-bright);
}

input {
    width: 100%;
    padding: 8px;
    border: 1px solid var(--border);
    border-radius: 4px;
    font-size: 1rem;
    background: var(--surface);
    color: var(--text);
    transition: border-color 0.15s;
}

input:focus {
    border-color: var(--accent);
    outline: none;
}

button:not(.card) {
    display: block;
    width: 100%;
    margin-top: 14px;
    padding: 18px 16px;
    background: var(--accent);
    color: var(--bg);
    border: none;
    border-radius: 8px;
    cursor: pointer;
    font-size: 1.1rem;
    font-weight: 600;
    font-family: inherit;
    transition: opacity 0.15s;
}

button:not(.card):active {
    opacity: 0.8;
}

.grid {
    display: grid;
    grid-template-columns: 1fr;
    gap: 12px;
    margin-top: 16px;
}

.card {
    padding: 18px 16px;
    background: var(--surface);
    border-radius: 8px;
    text-align: center;
    text-decoration: none;
    color: var(--text);
    font-weight: 600;
    font-size: 1.1rem;
    border: 1px solid var(--border);
    transition: background 0.15s, transform 0.1s;
}

.card:hover {
    background: var(--surface-hover);
    transform: translateY(-2px);
}

.card:active {
    transform: translateY(0);
}

.btn-back {
    display: block;
    width: 100%;
    margin-top: 14px;
    padding: 18px 16px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    text-align: center;
    text-decoration: none;
    color: var(--text);
    font-weight: 600;
    font-size: 1.1rem;
    transition: background 0.15s, transform 0.1s;
}

.btn-back:hover {
    background: var(--surface-hover);
    transform: translateY(-2px);
}

.btn-back:active {
    transform: translateY(0);
}

.card.danger {
    background: var(--surface);
    color: var(--danger);
    border-color: var(--danger-border);
    cursor: pointer;
    width: 100%;
    font-family: inherit;
    font-size: 1.1rem;
    font-weight: 600;
    padding: 18px 16px;
    border-radius: 8px;
    transition: background 0.15s, transform 0.1s;
}

.card.danger:hover {
    background: var(--surface-hover);
    transform: translateY(-2px);
}

.ok {
    color: var(--success);
    font-weight: 600;
    margin: 8px 0;
}

.err {
    color: var(--danger);
    font-weight: 600;
    margin: 8px 0;
}

.hint {
    color: var(--text);
    opacity: 0.85;
    font-size: 0.9rem;
    margin: 8px 0;
    line-height: 1.4;
}

ul {
    padding-left: 18px;
}

li {
    margin: 6px 0;
}

li a {
    color: var(--accent);
    text-decoration: none;
}

li a:hover {
    text-decoration: underline;
}

/* Checkboxes must not stretch to full width */
input[type="checkbox"] {
    width: auto;
}

/* Label with embedded checkbox */
.checkbox-label {
    display: flex;
    align-items: center;
    gap: 10px;
    margin: 12px 0;
    font-weight: 600;
    color: var(--text-bright);
    cursor: pointer;
}

.chaya-panel {
    margin-top: 20px;
    padding: 16px;
    border: 1px solid var(--border);
    border-radius: 8px;
    background: var(--surface);
}

.chaya-panel h2 {
    margin: 0 0 12px;
    font-size: 1rem;
    font-weight: 600;
    color: var(--text-bright);
    text-align: center;
    text-transform: lowercase;
}

.chaya-counters {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 10px;
}

.chaya-counter-box {
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 12px 8px;
    text-align: center;
    background: var(--bg);
}

.chaya-counter-label {
    font-size: 0.7rem;
    font-weight: 600;
    text-transform: uppercase;
    opacity: 0.75;
    margin-bottom: 6px;
    color: var(--text);
}

.chaya-counter-val {
    font-size: 1.6rem;
    font-weight: 700;
    color: var(--accent);
    line-height: 1.2;
}

/* Global toast bar (fixed bottom center) */
.toast {
    position: fixed;
    bottom: 24px;
    left: 50%;
    transform: translateX(-50%);
    padding: 12px 20px;
    border-radius: 8px;
    background: var(--surface);
    border: 1px solid var(--border);
    opacity: 0;
    pointer-events: none;
    transition: opacity 0.2s;
    white-space: nowrap;
    font-weight: 600;
    color: var(--text-bright);
    z-index: 10000;
}

.toast.show {
    opacity: 1;
}

.toast.err {
    color: var(--danger);
}
</style>)chaya2mqtt_css";
