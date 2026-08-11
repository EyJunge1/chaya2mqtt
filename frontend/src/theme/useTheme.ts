import { useSyncExternalStore } from "react";
import { getTheme, subscribeTheme, type Theme } from "./store";

export function useTheme(): Theme {
  return useSyncExternalStore(subscribeTheme, getTheme, () => "light" as Theme);
}
