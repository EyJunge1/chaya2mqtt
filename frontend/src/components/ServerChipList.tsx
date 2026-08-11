import { Plus, X } from "lucide-react";
import { useState } from "react";
import { cn } from "../ui/cn";
import { Badge } from "./Badge";
import { GhostButton, TextInput } from "./Form";
import { InfoTip } from "./InfoTip";

export function ServerChipList({
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
}) {
  const [draft, setDraft] = useState("");
  const [adding, setAdding] = useState(false);
  const canAdd = values.length < max;
  const showPreview = values.length === 0 && previewValues.length > 0;

  function commitDraft() {
    const next = draft.trim();
    if (!next || !validate(next) || values.includes(next) || !canAdd) return;
    onChange([...values, next]);
    setDraft("");
    setAdding(false);
  }

  return (
    <div className="space-y-2 rounded-lg border border-border p-3">
      <div className="flex items-center gap-1.5 text-sm font-semibold text-text-bright">
        {label}
        <InfoTip text={hint} />
      </div>

      {showPreview ? (
        <div className="flex flex-wrap gap-2">
          {previewValues.map((value) => (
            <Badge
              key={`preview-${value}`}
              tone="muted"
              dot
              data-testid={`${testIdPrefix}-preview`}
            >
              {value}
            </Badge>
          ))}
        </div>
      ) : null}

      {values.length > 0 ? (
        <div className="flex flex-wrap gap-2">
          {values.map((value) => (
            <Badge key={value} tone="neutral" data-testid={`${testIdPrefix}-chip`}>
              <button
                type="button"
                aria-label={`${removeLabel} ${value}`}
                data-testid={`${testIdPrefix}-remove`}
                onClick={() => onChange(values.filter((v) => v !== value))}
                className={cn(
                  "inline-flex shrink-0 text-danger transition hover:opacity-70 focus-ring",
                )}
              >
                <X size={12} strokeWidth={2.5} aria-hidden />
              </button>
              {value}
            </Badge>
          ))}
        </div>
      ) : null}

      {adding && canAdd ? (
        <div className="flex gap-2">
          <TextInput
            value={draft}
            onChange={(e) => setDraft(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter") {
                e.preventDefault();
                commitDraft();
              }
              if (e.key === "Escape") {
                setDraft("");
                setAdding(false);
              }
            }}
            placeholder={placeholder}
            inputMode={inputMode}
            maxLength={maxLength}
            autoFocus
            data-testid={`${testIdPrefix}-input`}
          />
          <GhostButton
            type="button"
            data-testid={`${testIdPrefix}-confirm`}
            onClick={commitDraft}
            disabled={!draft.trim() || !validate(draft.trim())}
          >
            {addLabel}
          </GhostButton>
        </div>
      ) : null}

      {!adding && canAdd ? (
        <GhostButton
          type="button"
          data-testid={`${testIdPrefix}-add`}
          onClick={() => setAdding(true)}
        >
          <Plus size={14} aria-hidden />
          {addLabel}
        </GhostButton>
      ) : null}
    </div>
  );
}
