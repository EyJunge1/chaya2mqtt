export function StatusBadge({
  ok,
  labelOk = 'Verbunden',
  labelBad = 'Getrennt',
}: {
  ok: boolean
  labelOk?: string
  labelBad?: string
}) {
  return (
    <span
      className={`inline-flex items-center gap-1.5 rounded-full px-2.5 py-1 text-xs font-semibold ${
        ok
          ? 'bg-accent/15 text-success'
          : 'bg-danger/15 text-danger'
      }`}
    >
      <span className={`h-1.5 w-1.5 rounded-full ${ok ? 'bg-success' : 'bg-danger'}`} />
      {ok ? labelOk : labelBad}
    </span>
  )
}
