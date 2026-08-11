import { RotateCcw, X } from "lucide-react";
import { useEffect, useState } from "react";
import { useLocation, useNavigate } from "react-router-dom";
import type { DeviceMode } from "../api/types";
import { cn } from "../ui/cn";
import { ACTIVE_ACCENT, HOVER_SURFACE } from "../ui/styles";

const scenarioGroups = [
  {
    title: "Device",
    items: [
      { id: "sta-connected", label: "STA online", path: "/" },
      { id: "boot-unreachable", label: "Boot unreachable", path: "/" },
      { id: "boot-slow", label: "Boot slow", path: "/" },
      { id: "sse-disconnected", label: "SSE reconnecting", path: "/" },
    ],
  },
  {
    title: "Network",
    items: [
      { id: "offline", label: "STA offline", path: "/" },
      { id: "sta-mqtt-offline", label: "MQTT offline", path: "/" },
      { id: "sta-mqtt-unconfigured", label: "MQTT unconfigured", path: "/mqtt" },
      { id: "sta-mqtt-unpaired", label: "MQTT unpaired", path: "/mqtt" },
    ],
  },
  {
    title: "Wi-Fi / Setup",
    items: [
      { id: "ap-setup", label: "AP Setup", path: "/" },
      { id: "wifi-scan-empty", label: "Scan empty", path: "/" },
      { id: "wifi-scan-fail", label: "Scan failed", path: "/" },
      { id: "ap-test-idle", label: "Test idle", path: "/wifi-testing" },
      { id: "ap-test-testing", label: "Test running", path: "/wifi-testing" },
      { id: "ap-test-ok", label: "Test ok", path: "/wifi-testing" },
      { id: "ap-test-failed", label: "Test failed", path: "/wifi-testing" },
    ],
  },
  {
    title: "Update",
    items: [
      { id: "update-available", label: "Update ready", path: "/update" },
      { id: "update-checking", label: "Checking", path: "/update" },
      { id: "update-busy", label: "Downloading", path: "/update" },
      { id: "update-verifying", label: "Verifying", path: "/update" },
      { id: "update-rebooting", label: "Rebooting", path: "/update" },
      { id: "update-error", label: "Update error", path: "/update" },
    ],
  },
] as const;

type ScenarioId = (typeof scenarioGroups)[number]["items"][number]["id"];

const faultGroups = [
  {
    title: "Load errors",
    items: [
      { id: "mqtt", label: "MQTT page", path: "/mqtt" },
      { id: "settings", label: "Settings page", path: "/settings/device" },
      { id: "update-status", label: "Update page", path: "/update" },
      { id: "device", label: "Device boot", path: "/" },
      { id: "sse", label: "SSE stream", path: "/" },
    ],
  },
  {
    title: "Action errors",
    items: [
      { id: "mqtt-save", label: "MQTT save", path: "/mqtt" },
      { id: "settings-save", label: "Settings save", path: "/settings/device" },
      { id: "reboot", label: "Reboot", path: "/settings/device" },
      { id: "factory-reset", label: "Factory reset", path: "/settings/device" },
      { id: "heart", label: "Send heart", path: "/" },
      { id: "wifi-scan", label: "Wi-Fi scan", path: "/wifi" },
      { id: "wifi-connect", label: "Wi-Fi connect", path: "/wifi" },
      { id: "wifi-commit", label: "Wi-Fi commit", path: "/wifi-testing" },
      { id: "wifi-abort", label: "Wi-Fi abort", path: "/wifi-testing" },
      { id: "update-check", label: "Update check", path: "/update" },
      { id: "update-install", label: "Update install", path: "/update" },
    ],
  },
] as const;

type FaultId = (typeof faultGroups)[number]["items"][number]["id"];

const pages = [
  { path: "/", label: "Dashboard" },
  { path: "/wifi", label: "Wi-Fi" },
  { path: "/wifi-testing", label: "Wi-Fi test" },
  { path: "/mqtt", label: "MQTT" },
  { path: "/settings", label: "Settings" },
  { path: "/settings/device", label: "Device" },
  { path: "/update", label: "Update" },
] as const;

export function MockToolbar({
  onChanged,
  mode = "sta",
  bootError = false,
}: {
  onChanged: () => Promise<void>;
  mode?: DeviceMode;
  bootError?: boolean;
}) {
  const [open, setOpen] = useState(true);
  const [busy, setBusy] = useState(false);
  const [activeScenario, setActiveScenario] = useState<ScenarioId>("sta-connected");
  const [activeFaults, setActiveFaults] = useState<Partial<Record<FaultId, boolean>>>({});
  const navigate = useNavigate();
  const location = useLocation();

  useEffect(() => {
    if (!import.meta.env.DEV) return;
    let cancelled = false;
    void (async () => {
      try {
        const res = await fetch("/api/_mock/state");
        if (!res.ok || cancelled) return;
        const data = (await res.json()) as {
          scenario?: ScenarioId;
          faults?: Partial<Record<FaultId, boolean>>;
        };
        if (data.scenario) setActiveScenario(data.scenario);
        if (data.faults) setActiveFaults(data.faults);
      } catch {
        /* ignore sync failures */
      }
    })();
    return () => {
      cancelled = true;
    };
  }, [bootError, mode]);

  if (!import.meta.env.DEV) return null;

  const visiblePages =
    mode === "ap"
      ? [
          { path: "/", label: "Setup" },
          { path: "/wifi-testing", label: "Wi-Fi test" },
        ]
      : pages;

  async function setScenario(scenario: { id: ScenarioId; path: string }) {
    setBusy(true);
    try {
      const body = new URLSearchParams({ scenario: scenario.id });
      const response = await fetch("/api/_mock/scenario", { method: "POST", body });
      if (!response.ok) throw new Error(`scenario failed (${response.status})`);
      setActiveScenario(scenario.id);
      setActiveFaults({});
      navigate(scenario.path);
      await onChanged();
    } finally {
      setBusy(false);
    }
  }

  async function toggleFault(fault: { id: FaultId; path: string }) {
    setBusy(true);
    try {
      const enabled = !activeFaults[fault.id];
      const body = new URLSearchParams({
        fault: fault.id,
        enabled: enabled ? "1" : "0",
      });
      const response = await fetch("/api/_mock/fault", { method: "POST", body });
      if (!response.ok) throw new Error(`fault failed (${response.status})`);
      const data = (await response.json()) as { faults?: Partial<Record<FaultId, boolean>> };
      if (data.faults) setActiveFaults(data.faults);
      else setActiveFaults((prev) => ({ ...prev, [fault.id]: enabled }));
      navigate(fault.path);
      await onChanged();
    } finally {
      setBusy(false);
    }
  }

  async function clearAllFaults() {
    setBusy(true);
    try {
      const body = new URLSearchParams({ clear: "1" });
      const response = await fetch("/api/_mock/fault", { method: "POST", body });
      if (!response.ok) throw new Error(`clear faults failed (${response.status})`);
      setActiveFaults({});
      await onChanged();
    } finally {
      setBusy(false);
    }
  }

  async function reset() {
    setBusy(true);
    try {
      const response = await fetch("/api/_mock/reset", { method: "POST" });
      if (!response.ok) throw new Error(`reset failed (${response.status})`);
      setActiveScenario("sta-connected");
      setActiveFaults({});
      navigate("/");
      await onChanged();
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="fixed right-3 top-3 z-40 w-[min(16rem,calc(100vw-1.5rem))] rounded-xl border border-border bg-surface/95 p-2 text-xs shadow-lg backdrop-blur">
      <button
        type="button"
        className="flex w-full items-center justify-between px-1 py-0.5 text-left font-semibold text-accent"
        onClick={() => setOpen((v) => !v)}
      >
        <span>Simulator{bootError ? " · offline" : ""}</span>
        {open ? <X size={14} /> : <span>Open</span>}
      </button>
      {open ? (
        <div className="mt-2 max-h-[min(75vh,36rem)] space-y-3 overflow-y-auto border-t border-border pt-2">
          {scenarioGroups.map((group) => (
            <section key={group.title}>
              <p className="mb-1 px-2 text-[10px] font-semibold uppercase tracking-wider text-muted">
                {group.title}
              </p>
              <div className="space-y-1">
                {group.items.map((scenario) => {
                  const active = activeScenario === scenario.id;
                  return (
                    <button
                      key={scenario.id}
                      type="button"
                      disabled={busy}
                      className={cn(
                        "block w-full rounded-md px-2 py-1.5 text-left disabled:opacity-50",
                        active
                          ? cn(ACTIVE_ACCENT, "font-semibold")
                          : cn("text-muted", HOVER_SURFACE),
                      )}
                      onClick={() => void setScenario(scenario)}
                    >
                      {scenario.label}
                    </button>
                  );
                })}
              </div>
            </section>
          ))}

          {faultGroups.map((group) => (
            <section key={group.title}>
              <p className="mb-1 px-2 text-[10px] font-semibold uppercase tracking-wider text-muted">
                {group.title}
              </p>
              <div className="space-y-1">
                {group.items.map((fault) => {
                  const active = Boolean(activeFaults[fault.id]);
                  return (
                    <button
                      key={fault.id}
                      type="button"
                      disabled={busy}
                      className={cn(
                        "block w-full rounded-md px-2 py-1.5 text-left disabled:opacity-50",
                        active
                          ? "bg-danger/15 font-semibold text-danger"
                          : cn("text-muted", HOVER_SURFACE),
                      )}
                      onClick={() => void toggleFault(fault)}
                    >
                      {active ? "● " : ""}
                      {fault.label}
                    </button>
                  );
                })}
              </div>
            </section>
          ))}

          <section>
            <p className="mb-1 px-2 text-[10px] font-semibold uppercase tracking-wider text-muted">
              Open page
            </p>
            <div className="grid grid-cols-2 gap-1">
              {visiblePages.map((page) => (
                <button
                  key={page.path}
                  type="button"
                  disabled={busy}
                  className={cn(
                    "rounded-md px-2 py-1.5 text-left",
                    location.pathname === page.path
                      ? ACTIVE_ACCENT
                      : cn("text-muted", HOVER_SURFACE),
                  )}
                  onClick={() => navigate(page.path)}
                >
                  {page.label}
                </button>
              ))}
            </div>
          </section>

          <button
            type="button"
            disabled={busy}
            className={cn(
              "flex w-full items-center gap-1.5 rounded-md border border-border px-2 py-1.5 text-muted disabled:opacity-50",
              HOVER_SURFACE,
            )}
            onClick={() => void clearAllFaults()}
          >
            Clear faults
          </button>

          <button
            type="button"
            disabled={busy}
            className={cn(
              "flex w-full items-center gap-1.5 rounded-md border border-border px-2 py-1.5 text-muted disabled:opacity-50",
              HOVER_SURFACE,
            )}
            onClick={() => void reset()}
          >
            <RotateCcw size={12} />
            Reset simulator
          </button>
        </div>
      ) : null}
    </div>
  );
}
