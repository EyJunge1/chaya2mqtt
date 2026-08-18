export type NavigateOptions = {
  replace?: boolean;
};

function pathOf(to: string): string {
  const path = to.split("?")[0] ?? "/";
  return path.length > 0 ? path : "/";
}

class Router {
  pathname = $state(typeof window !== "undefined" ? window.location.pathname : "/");

  navigate(to: string, opts: NavigateOptions = {}): void {
    const path = pathOf(to);
    if (typeof history !== "undefined") {
      if (opts.replace) history.replaceState({}, "", to);
      else history.pushState({}, "", to);
    }
    this.pathname = path;
  }

  replace(to: string): void {
    this.navigate(to, { replace: true });
  }

  syncFromLocation(): void {
    if (typeof window !== "undefined") {
      this.pathname = window.location.pathname;
    }
  }
}

export const router = new Router();

if (typeof window !== "undefined") {
  window.addEventListener("popstate", () => {
    router.syncFromLocation();
  });
}

export const KNOWN_ROUTES = new Set([
  "/",
  "/wifi",
  "/wifi-testing",
  "/mqtt",
  "/settings",
  "/settings/device",
  "/update",
]);
