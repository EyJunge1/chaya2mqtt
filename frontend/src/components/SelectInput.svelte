<script lang="ts">
  import type { HTMLSelectAttributes } from "svelte/elements";
  import { cn } from "../ui/cn.ts";

  let {
    value = $bindable(""),
    class: className = "",
    children,
    onchange,
    ...rest
  }: HTMLSelectAttributes & { children: import("svelte").Snippet } = $props();
</script>

<select
  {value}
  class={cn(
    "w-full rounded-lg border border-border bg-bg px-3 py-2.5 text-text outline-none transition focus:border-accent",
    className,
  )}
  onchange={(event) => {
    value = event.currentTarget.value;
    onchange?.(event);
  }}
  {...rest}
>
  {@render children()}
</select>
