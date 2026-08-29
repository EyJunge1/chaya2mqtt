<script lang="ts">
  import { i18n } from "../i18n/i18n.svelte.ts";
  import type { ToastItem } from "./toastStack.ts";
  import ToastCard from "./ToastCard.svelte";

  const STACK_PEEK = 12;
  const STACK_SCALE = 0.05;
  const FRONT_HEIGHT_REM = 3.75;

  let {
    toasts,
    onDismiss,
  }: {
    toasts: ToastItem[];
    onDismiss: (id: string) => void;
  } = $props();

  let expanded = $state(false);
  const ordered = $derived([...toasts].reverse());
  const visibleBehind = $derived(Math.min(Math.max(ordered.length - 1, 0), 2));
</script>

{#if toasts.length > 0}
  <div
    role="region"
    aria-label={i18n.t("toast.region")}
    class="pointer-events-none fixed right-4 bottom-[max(4.75rem,calc(env(safe-area-inset-bottom)+3.75rem))] left-4 z-50 sm:bottom-[max(1rem,env(safe-area-inset-bottom))] sm:left-auto sm:w-88"
    onmouseenter={() => (expanded = true)}
    onmouseleave={() => (expanded = false)}
    aria-relevant="additions"
  >
    <div
      class={`pointer-events-auto relative w-full transition-[height] duration-300 ease-out ${
        expanded ? "flex flex-col-reverse gap-2" : ""
      }`}
      style={expanded
        ? undefined
        : `height: calc(${FRONT_HEIGHT_REM}rem + ${visibleBehind * STACK_PEEK}px)`}
    >
      {#each ordered as item, index (item.id)}
        {@const front = index === 0}
        {@const behind = Math.min(index, 2)}
        {@const hiddenBehind = !expanded && index > 2}
        {#if expanded}
          <div class="w-full transition-[transform,opacity] duration-300 ease-out">
            <ToastCard {item} {onDismiss} />
          </div>
        {:else}
          <div
            aria-hidden={!front}
            class={`absolute right-0 bottom-0 left-0 origin-bottom transition-[transform,opacity] duration-300 ease-out ${
              front ? "animate-[toast-in_180ms_ease-out]" : ""
            } ${hiddenBehind ? "pointer-events-none opacity-0" : ""}`}
            style="z-index: {ordered.length - index}; transform: translateY(-{behind *
              STACK_PEEK}px) scale({1 - behind * STACK_SCALE})"
          >
            <ToastCard {item} {onDismiss} dimmed={!front} />
          </div>
        {/if}
      {/each}
    </div>
  </div>
{/if}
