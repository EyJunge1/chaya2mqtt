export type ButtonWidth = "full" | "auto";

export function widthClass(width: ButtonWidth = "full"): string {
  return width === "full" ? "w-full" : "w-auto self-start";
}
