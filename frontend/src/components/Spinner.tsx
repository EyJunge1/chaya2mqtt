import { LoaderCircle } from "lucide-react";
import { cn } from "../ui/cn";

export function Spinner({ size = 18, className }: { size?: number; className?: string }) {
  return (
    <LoaderCircle size={size} className={cn("animate-spin text-accent", className)} aria-hidden />
  );
}
