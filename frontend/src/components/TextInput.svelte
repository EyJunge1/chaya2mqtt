<script lang="ts">
  import type { HTMLInputAttributes } from "svelte/elements";
  import { cn } from "../ui/cn.ts";

  let {
    value = $bindable(""),
    class: className = "",
    oninput,
    onchange,
    ...rest
  }: HTMLInputAttributes & { value?: string | number } = $props();

  function sync(event: Event & { currentTarget: HTMLInputElement }) {
    const next =
      rest.type === "number" ? event.currentTarget.valueAsNumber : event.currentTarget.value;
    value =
      Number.isNaN(next as number) && rest.type === "number" ? event.currentTarget.value : next;
  }
</script>

<input
  {value}
  class={cn(
    "w-full rounded-lg border border-border bg-bg px-3 py-2.5 text-text outline-none transition focus:border-accent",
    className,
  )}
  oninput={(event) => {
    sync(event);
    oninput?.(event);
  }}
  onchange={(event) => {
    sync(event);
    onchange?.(event);
  }}
  {...rest}
/>
