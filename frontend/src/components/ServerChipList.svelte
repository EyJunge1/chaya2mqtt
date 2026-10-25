<script lang="ts">
  import { Plus, X } from "@lucide/svelte";
  import { cn } from "../ui/cn.ts";
  import Badge from "./Badge.svelte";
  import GhostButton from "./GhostButton.svelte";
  import InfoTip from "./InfoTip.svelte";
  import TextInput from "./TextInput.svelte";

  let {
    label,
    values,
    onChange,
    max = 2,
    placeholder,
    validate,
    hint,
    previewValues = [],
    addLabel,
    removeLabel,
    testIdPrefix,
    inputMode,
    maxLength,
  }: {
    label: string;
    values: string[];
    onChange: (next: string[]) => void;
    max?: number;
    placeholder: string;
    validate: (value: string) => boolean;
    hint: string;
    previewValues?: string[];
    addLabel: string;
    removeLabel: string;
    testIdPrefix: string;
    inputMode?: "decimal" | "text";
    maxLength?: number;
  } = $props();

  let draft = $state("");
  let adding = $state(false);
  const canAdd = $derived(values.length < max);
  const showPreview = $derived(values.length === 0 && previewValues.length > 0);

  function commitDraft() {
    const next = draft.trim();
    if (!next || !validate(next) || values.includes(next) || !canAdd) return;
    onChange([...values, next]);
    draft = "";
    adding = false;
  }
</script>

<div class="space-y-2 rounded-lg border border-border p-3">
  <div class="flex items-center gap-1.5 text-sm font-semibold text-text-bright">
    {label}
    <InfoTip text={hint} />
  </div>

  {#if showPreview}
    <div class="flex flex-wrap gap-2">
      {#each previewValues as value (value)}
        <Badge tone="muted" dot data-testid={`${testIdPrefix}-preview`}>
          {value}
        </Badge>
      {/each}
    </div>
  {/if}

  {#if values.length > 0}
    <div class="flex flex-wrap gap-2">
      {#each values as value (value)}
        <Badge tone="neutral" data-testid={`${testIdPrefix}-chip`}>
          <button
            type="button"
            aria-label={`${removeLabel} ${value}`}
            data-testid={`${testIdPrefix}-remove`}
            onclick={() => onChange(values.filter((v) => v !== value))}
            class={cn("inline-flex shrink-0 text-danger transition hover:opacity-70 focus-ring")}
          >
            <X size={12} strokeWidth={2.5} aria-hidden="true" />
          </button>
          {value}
        </Badge>
      {/each}
    </div>
  {/if}

  {#if adding && canAdd}
    <div class="flex gap-2">
      <TextInput
        bind:value={draft}
        onkeydown={(e) => {
          if (e.key === "Enter") {
            e.preventDefault();
            commitDraft();
          }
          if (e.key === "Escape") {
            draft = "";
            adding = false;
          }
        }}
        {placeholder}
        inputmode={inputMode}
        maxlength={maxLength}
        autofocus
        data-testid={`${testIdPrefix}-input`}
      />
      <GhostButton
        type="button"
        data-testid={`${testIdPrefix}-confirm`}
        onclick={commitDraft}
        disabled={!draft.trim() || !validate(draft.trim())}
      >
        {addLabel}
      </GhostButton>
    </div>
  {/if}

  {#if !adding && canAdd}
    <GhostButton type="button" data-testid={`${testIdPrefix}-add`} onclick={() => (adding = true)}>
      <Plus size={14} aria-hidden="true" />
      {addLabel}
    </GhostButton>
  {/if}
</div>
