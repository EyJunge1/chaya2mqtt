<script lang="ts">
  import Alert from "./Alert.svelte";
  import SecondaryButton from "./SecondaryButton.svelte";

  let {
    title,
    message,
    retryLabel,
    onRetry,
  }: {
    title: string;
    message: string;
    retryLabel: string;
    onRetry: () => void;
  } = $props();
</script>

<div class="space-y-3">
  <Alert variant="error" {title}>
    {message}
  </Alert>
  <SecondaryButton type="button" onclick={onRetry}>
    {retryLabel}
  </SecondaryButton>
</div>
