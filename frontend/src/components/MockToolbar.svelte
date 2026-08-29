<script lang="ts">
  import { RotateCcw, X } from "@lucide/svelte";
  import type { DeviceMode } from "../api/types.ts";
  import { router } from "../nav/router.svelte.ts";
  import { cn } from "../ui/cn.ts";
  import { ACTIVE_ACCENT, HOVER_SURFACE } from "../ui/styles.ts";

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
        { id: "ap-test-testing", label: "Wi-Fi test", path: "/wifi-testing" },
        { id: "ap-test-ok", label: "Wi-Fi test success", path: "/wifi-testing" },
        { id: "ap-test-failed", label: "Wi-Fi test failed", path: "/wifi-testing" },
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

  const loadFaultItems = [
    { id: "mqtt", label: "MQTT load fail", path: "/mqtt" },
    { id: "settings", label: "Settings load fail", path: "/settings/device" },
    { id: "update-status", label: "Update load fail", path: "/update" },
    { id: "device", label: "Device boot fail", path: "/" },
    { id: "sse", label: "SSE stream fail", path: "/" },
  ] as const;

  const actionFaultItems = [
    { id: "mqtt-save", label: "MQTT save", path: "/mqtt" },
    { id: "settings-save", label: "Settings save", path: "/settings/device" },
    { id: "reboot", label: "Reboot", path: "/settings/device" },
    { id: "factory-reset", label: "Factory reset", path: "/settings/device" },
    { id: "heart", label: "Send heart", path: "/" },
    { id: "wifi-scan", label: "Wi-Fi scan", path: "/wifi" },
    { id: "wifi-connect", label: "Wi-Fi test start", path: "/" },
    { id: "wifi-commit", label: "Wi-Fi test save", path: "/wifi-testing" },
    { id: "wifi-retry", label: "Wi-Fi test retry", path: "/wifi-testing" },
    { id: "update-check", label: "Update check", path: "/update" },
    { id: "update-install", label: "Update install", path: "/update" },
  ] as const;

  type LoadFaultId = (typeof loadFaultItems)[number]["id"];
  type ActionFaultId = (typeof actionFaultItems)[number]["id"];
  type FaultId = LoadFaultId | ActionFaultId;

  const pages = [
    { path: "/", label: "Dashboard" },
    { path: "/wifi", label: "Wi-Fi" },
    { path: "/wifi-testing", label: "Testing page" },
    { path: "/mqtt", label: "MQTT" },
    { path: "/settings", label: "Settings" },
    { path: "/settings/device", label: "Device" },
    { path: "/update", label: "Update" },
  ] as const;

  let {
    onChanged,
    mode = "sta",
    bootError = false,
  }: {
    onChanged: () => Promise<void>;
    mode?: DeviceMode;
    bootError?: boolean;
  } = $props();

  let open = $state(true);
  let busy = $state(false);
  let activeScenario = $state<ScenarioId>("sta-connected");
  let activeFaults = $state<Partial<Record<FaultId, boolean>>>({});

  const visiblePages = $derived(
    mode === "ap"
      ? [
          { path: "/", label: "Setup" },
          { path: "/wifi-testing", label: "Testing page" },
        ]
      : pages,
  );

  /** At most one load fault; when set, scenario buttons are not highlighted. */
  const selectedLoadFault = $derived(
    loadFaultItems.find((item) => activeFaults[item.id])?.id ?? null,
  );

  $effect(() => {
    if (!import.meta.env.DEV) return;
    void bootError;
    void mode;
    let cancelled = false;
    void (async () => {
      try {
        const res = await fetch("/api/_mock/state");
        if (!res.ok || cancelled) return;
        const data = (await res.json()) as {
          scenario?: ScenarioId;
          faults?: Partial<Record<FaultId, boolean>>;
        };
        if (data.scenario) activeScenario = data.scenario;
        if (data.faults) activeFaults = data.faults;
      } catch {
        /* ignore sync failures */
      }
    })();
    return () => {
      cancelled = true;
    };
  });

  async function setScenario(scenario: { id: ScenarioId; path: string }) {
    busy = true;
    try {
      const body = new URLSearchParams({ scenario: scenario.id });
      const response = await fetch("/api/_mock/scenario", { method: "POST", body });
      if (!response.ok) throw new Error(`scenario failed (${response.status})`);
      activeScenario = scenario.id;
      activeFaults = {};
      router.navigate(scenario.path);
      await onChanged();
    } finally {
      busy = false;
    }
  }

  async function postFault(id: FaultId, enabled: boolean) {
    const response = await fetch("/api/_mock/fault", {
      method: "POST",
      body: new URLSearchParams({ fault: id, enabled: enabled ? "1" : "0" }),
    });
    if (!response.ok) throw new Error(`fault failed (${response.status})`);
    const data = (await response.json()) as { faults?: Partial<Record<FaultId, boolean>> };
    if (data.faults) activeFaults = data.faults;
    else activeFaults = { ...activeFaults, [id]: enabled };
  }

  /** Load errors are exclusive with each other and with scenarios. */
  async function setLoadFault(fault: { id: LoadFaultId; path: string }) {
    busy = true;
    try {
      if (activeFaults[fault.id]) {
        await postFault(fault.id, false);
      } else {
        // Drop the scenario selection so only this load fault is highlighted.
        const scenarioBody = new URLSearchParams({ scenario: "sta-connected" });
        const scenarioResponse = await fetch("/api/_mock/scenario", {
          method: "POST",
          body: scenarioBody,
        });
        if (!scenarioResponse.ok) {
          throw new Error(`scenario failed (${scenarioResponse.status})`);
        }
        activeScenario = "sta-connected";
        activeFaults = {};
        await postFault(fault.id, true);
      }
      router.navigate(fault.path);
      await onChanged();
    } finally {
      busy = false;
    }
  }

  async function toggleActionFault(fault: { id: ActionFaultId; path: string }) {
    busy = true;
    try {
      await postFault(fault.id, !activeFaults[fault.id]);
      router.navigate(fault.path);
      await onChanged();
    } finally {
      busy = false;
    }
  }

  async function clearAllFaults() {
    busy = true;
    try {
      const body = new URLSearchParams({ clear: "1" });
      const response = await fetch("/api/_mock/fault", { method: "POST", body });
      if (!response.ok) throw new Error(`clear faults failed (${response.status})`);
      activeFaults = {};
      await onChanged();
    } finally {
      busy = false;
    }
  }

  async function reset() {
    busy = true;
    try {
      const response = await fetch("/api/_mock/reset", { method: "POST" });
      if (!response.ok) throw new Error(`reset failed (${response.status})`);
      activeScenario = "sta-connected";
      activeFaults = {};
      router.navigate("/");
      await onChanged();
    } finally {
      busy = false;
    }
  }
</script>

{#if import.meta.env.DEV}
  <div
    class="fixed right-3 top-3 z-40 w-[min(16rem,calc(100vw-1.5rem))] rounded-xl border border-border bg-surface/95 p-2 text-xs shadow-lg backdrop-blur"
  >
    <button
      type="button"
      class="flex w-full items-center justify-between px-1 py-0.5 text-left font-semibold text-accent"
      onclick={() => (open = !open)}
    >
      <span>Simulator{bootError ? " · offline" : ""}</span>
      {#if open}
        <X size={14} />
      {:else}
        <span>Open</span>
      {/if}
    </button>
    {#if open}
      <div
        class="mt-2 max-h-[min(75vh,36rem)] space-y-5 overflow-y-auto border-t border-border pt-2"
      >
        {#each scenarioGroups as group (group.title)}
          <section>
            <p class="mb-1.5 px-2 text-xs font-semibold uppercase tracking-wider text-muted">
              {group.title}
            </p>
            <div class="space-y-1">
              {#each group.items as scenario (scenario.id)}
                {@const active = selectedLoadFault === null && activeScenario === scenario.id}
                <button
                  type="button"
                  disabled={busy}
                  class={cn(
                    "block w-full rounded-md px-2 py-1.5 text-left disabled:opacity-50",
                    active ? cn(ACTIVE_ACCENT, "font-semibold") : cn("text-muted", HOVER_SURFACE),
                  )}
                  onclick={() => void setScenario(scenario)}
                >
                  {scenario.label}
                </button>
              {/each}
            </div>
          </section>
        {/each}

        <section>
          <p class="mb-1.5 px-2 text-xs font-semibold uppercase tracking-wider text-muted">
            Load errors
          </p>
          <div class="space-y-1">
            {#each loadFaultItems as fault (fault.id)}
              {@const active = selectedLoadFault === fault.id}
              <button
                type="button"
                disabled={busy}
                class={cn(
                  "block w-full rounded-md px-2 py-1.5 text-left disabled:opacity-50",
                  active ? cn(ACTIVE_ACCENT, "font-semibold") : cn("text-muted", HOVER_SURFACE),
                )}
                onclick={() => void setLoadFault(fault)}
              >
                {fault.label}
              </button>
            {/each}
          </div>
        </section>

        <section>
          <p class="mb-1.5 px-2 text-xs font-semibold uppercase tracking-wider text-muted">
            Action errors
          </p>
          <div class="space-y-1">
            {#each actionFaultItems as fault (fault.id)}
              {@const active = Boolean(activeFaults[fault.id])}
              <button
                type="button"
                disabled={busy}
                class={cn(
                  "block w-full rounded-md px-2 py-1.5 text-left disabled:opacity-50",
                  active
                    ? "bg-danger/15 font-semibold text-danger"
                    : cn("text-muted", HOVER_SURFACE),
                )}
                onclick={() => void toggleActionFault(fault)}
              >
                {active ? "● " : ""}{fault.label}
              </button>
            {/each}
          </div>
        </section>

        <section>
          <p class="mb-1.5 px-2 text-xs font-semibold uppercase tracking-wider text-muted">
            Open page
          </p>
          <div class="grid grid-cols-2 gap-1">
            {#each visiblePages as page (page.path)}
              <button
                type="button"
                disabled={busy}
                class={cn(
                  "rounded-md px-2 py-1.5 text-left",
                  router.pathname === page.path ? ACTIVE_ACCENT : cn("text-muted", HOVER_SURFACE),
                )}
                onclick={() => router.navigate(page.path)}
              >
                {page.label}
              </button>
            {/each}
          </div>
        </section>

        <button
          type="button"
          disabled={busy}
          class={cn(
            "flex w-full items-center gap-1.5 rounded-md border border-border px-2 py-1.5 text-muted disabled:opacity-50",
            HOVER_SURFACE,
          )}
          onclick={() => void clearAllFaults()}
        >
          Clear faults
        </button>

        <button
          type="button"
          disabled={busy}
          class={cn(
            "flex w-full items-center gap-1.5 rounded-md border border-border px-2 py-1.5 text-muted disabled:opacity-50",
            HOVER_SURFACE,
          )}
          onclick={() => void reset()}
        >
          <RotateCcw size={12} />
          Reset simulator
        </button>
      </div>
    {/if}
  </div>
{/if}
