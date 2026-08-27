<script lang="ts">
  import DangerButton from "./DangerButton.svelte";
  import PrimaryButton from "./PrimaryButton.svelte";
  import SecondaryButton from "./SecondaryButton.svelte";

  let {
    open,
    title,
    description,
    confirmLabel,
    cancelLabel,
    confirming = false,
    confirmVariant = "danger",
    onConfirm,
    onCancel,
  }: {
    open: boolean;
    title: string;
    description: string;
    confirmLabel: string;
    cancelLabel: string;
    confirming?: boolean;
    confirmVariant?: "danger" | "primary";
    onConfirm: () => void;
    onCancel: () => void;
  } = $props();

  const ids = $props.id();
  const titleId = `${ids}-title`;
  const descriptionId = `${ids}-desc`;
  let dialogEl: HTMLDialogElement | undefined = $state();

  $effect(() => {
    const el = dialogEl;
    if (!el) return;
    if (open && !el.open) {
      if (typeof el.showModal === "function") el.showModal();
      else el.setAttribute("open", "");
    }
    if (!open && el.open) {
      if (typeof el.close === "function") el.close();
      else el.removeAttribute("open");
    }
  });
</script>

<dialog
  bind:this={dialogEl}
  aria-labelledby={titleId}
  aria-describedby={descriptionId}
  oncancel={(e) => {
    e.preventDefault();
    if (!confirming) onCancel();
  }}
  class="m-auto w-[min(92vw,24rem)] rounded-2xl border border-border bg-surface p-0 text-text shadow-dialog backdrop:bg-black/55 open:flex open:flex-col"
>
  <div class="space-y-2 px-5 pt-5 pb-4">
    <h2 id={titleId} class="text-lg font-semibold text-text-bright">
      {title}
    </h2>
    <p id={descriptionId} class="text-sm text-muted">
      {description}
    </p>
  </div>
  <div class="flex flex-col gap-2 border-t border-border px-5 py-4 sm:flex-row-reverse">
    {#if confirmVariant === "primary"}
      <PrimaryButton
        type="button"
        loading={confirming}
        onclick={onConfirm}
        class="sm:w-auto sm:min-w-32 sm:px-5"
        width="full"
      >
        {confirmLabel}
      </PrimaryButton>
    {:else}
      <DangerButton
        type="button"
        loading={confirming}
        onclick={onConfirm}
        class="sm:w-auto sm:min-w-32 sm:px-5"
        width="full"
      >
        {confirmLabel}
      </DangerButton>
    {/if}
    <SecondaryButton
      type="button"
      disabled={confirming}
      onclick={onCancel}
      class="sm:w-auto sm:min-w-32 sm:px-5"
      width="full"
    >
      {cancelLabel}
    </SecondaryButton>
  </div>
</dialog>
