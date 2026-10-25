<script lang="ts">
  import type { HTMLAnchorAttributes } from "svelte/elements";
  import { router } from "../nav/router.svelte.ts";
  import { cn } from "../ui/cn.ts";

  let {
    href,
    replace = false,
    end = false,
    class: className = "",
    children,
    onclick,
    ...rest
  }: HTMLAnchorAttributes & {
    href: string;
    replace?: boolean;
    end?: boolean;
    children: import("svelte").Snippet;
  } = $props();

  const active = $derived(
    end
      ? router.pathname === href
      : router.pathname === href || router.pathname.startsWith(`${href}/`),
  );

  function handleClick(event: MouseEvent & { currentTarget: EventTarget & HTMLAnchorElement }) {
    onclick?.(event);
    if (event.defaultPrevented) return;
    if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey || event.button !== 0) {
      return;
    }
    event.preventDefault();
    router.navigate(href, { replace });
  }
</script>

<a
  {href}
  aria-current={active ? "page" : undefined}
  class={cn(className)}
  onclick={handleClick}
  {...rest}
>
  {@render children()}
</a>
