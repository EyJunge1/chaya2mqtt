import {
  ArrowLeft,
  ChevronRight,
  Heart,
  PanelLeftClose,
  PanelLeftOpen,
  Settings,
  type LucideIcon,
} from "lucide-react";
import { useEffect, useState, type ReactNode } from "react";
import { Link, NavLink, useLocation } from "react-router-dom";
import type { DeviceMode } from "../api/types";
import { useI18n } from "../i18n/useI18n";
import type { TranslationKey } from "../i18n/translations";
import { settingsNavItems } from "../nav/settingsNav";
import type { LiveState } from "../state/deviceContext";
import { cn } from "../ui/cn";
import { ACTIVE_ACCENT, HOVER_SURFACE, ICON_WELL } from "../ui/styles";
import { Alert } from "./Alert";
import { IconButton } from "./IconButton";
import { ShellFooter, ShellUtilityButtons } from "./ShellFooter";

const SIDEBAR_COLLAPSED_KEY = "chaya2mqtt-sidebar-collapsed";
const SETTINGS_OPEN_KEY = "chaya2mqtt-settings-nav-open";

type NavItem = {
  to: string;
  labelKey: TranslationKey;
  icon: LucideIcon;
};

const homeItem: NavItem = { to: "/", labelKey: "nav.dashboard", icon: Heart };

const settingsChildren: NavItem[] = settingsNavItems.map(({ to, labelKey, icon }) => ({
  to,
  labelKey,
  icon,
}));

const apNavItems: NavItem[] = [{ to: "/", labelKey: "nav.setup", icon: settingsNavItems[0].icon }];

const titleKeys: Record<string, TranslationKey> = {
  "/": "nav.dashboard",
  "/wifi": "nav.wifi",
  "/wifi-testing": "nav.wifi-testing",
  "/mqtt": "nav.mqtt",
  "/settings": "nav.settings",
  "/settings/device": "nav.device",
  "/update": "nav.update",
};

function isSettingsPath(pathname: string): boolean {
  return (
    pathname === "/settings" ||
    settingsChildren.some((item) => item.to === pathname) ||
    pathname === "/wifi-testing"
  );
}

function isSettingsChildPath(pathname: string): boolean {
  return settingsChildren.some((item) => item.to === pathname) || pathname === "/wifi-testing";
}

function readCollapsed(): boolean {
  try {
    return localStorage.getItem(SIDEBAR_COLLAPSED_KEY) === "1";
  } catch {
    return false;
  }
}

function readSettingsOpen(): boolean {
  try {
    return localStorage.getItem(SETTINGS_OPEN_KEY) !== "0";
  } catch {
    return true;
  }
}

function navClass(active: boolean, collapsed: boolean): string {
  return cn(
    "flex items-center rounded-xl text-sm font-semibold transition focus-ring",
    collapsed ? "justify-center px-0 py-2.5" : "gap-3 px-3 py-2.5",
    active ? ACTIVE_ACCENT : cn("text-text", HOVER_SURFACE),
  );
}

function mobileNavClass(active: boolean): string {
  return cn(
    "flex min-w-0 flex-1 flex-col items-center gap-1 rounded-lg px-1 py-2 text-[0.65rem] font-semibold transition focus-ring",
    active ? "text-accent" : "text-muted hover:text-text-bright",
  );
}

function NavItemLink({
  item,
  collapsed,
  updateAvailable,
}: {
  item: NavItem;
  collapsed: boolean;
  updateAvailable: boolean;
}) {
  const { t } = useI18n();
  const label = t(item.labelKey);
  const showDot = updateAvailable && item.to === "/update";
  return (
    <NavLink
      to={item.to}
      end={item.to === "/"}
      title={label}
      aria-label={showDot ? `${label} (${t("dashboard.update-available-title")})` : label}
      className={({ isActive }) => navClass(isActive, collapsed)}
    >
      {!collapsed ? (
        <span className="flex min-w-0 flex-1 items-center gap-2">
          <span className="truncate">{label}</span>
          {showDot ? (
            <span className="size-2 shrink-0 rounded-full bg-warning" aria-hidden />
          ) : null}
        </span>
      ) : null}
      <span className="relative inline-flex shrink-0">
        <item.icon size={18} aria-hidden />
        {showDot && collapsed ? (
          <span
            className="absolute -top-0.5 -right-0.5 size-2 rounded-full bg-warning"
            aria-hidden
          />
        ) : null}
      </span>
    </NavLink>
  );
}

export function Layout({
  children,
  mode = "sta",
  live = "live",
  updateAvailable = false,
}: {
  children: ReactNode;
  mode?: DeviceMode;
  live?: LiveState;
  updateAvailable?: boolean;
}) {
  const { pathname } = useLocation();
  const { t } = useI18n();
  const settingsActive = isSettingsPath(pathname);
  const settingsChild = isSettingsChildPath(pathname);
  const title =
    mode === "ap" && (pathname === "/" || pathname === "/wifi")
      ? t("nav.setup")
      : pathname === "/"
        ? t("app.title")
        : t(titleKeys[pathname] ?? "nav.dashboard");
  const hideChrome = pathname === "/wifi-testing";
  const [collapsed, setCollapsed] = useState(readCollapsed);
  const [settingsOpen, setSettingsOpen] = useState(() => readSettingsOpen() || settingsActive);

  useEffect(() => {
    try {
      localStorage.setItem(SIDEBAR_COLLAPSED_KEY, collapsed ? "1" : "0");
    } catch {
      /* ignore */
    }
  }, [collapsed]);

  useEffect(() => {
    try {
      localStorage.setItem(SETTINGS_OPEN_KEY, settingsOpen ? "1" : "0");
    } catch {
      /* ignore */
    }
  }, [settingsOpen]);

  useEffect(() => {
    if (settingsActive) setSettingsOpen(true);
  }, [settingsActive]);

  function toggleSettingsGroup() {
    setSettingsOpen((prev) => !prev);
  }

  return (
    <div className="min-h-screen bg-bg lg:flex lg:h-dvh lg:min-h-0 lg:overflow-hidden">
      {!hideChrome ? (
        <aside
          className={cn(
            "hidden h-full shrink-0 flex-col overflow-hidden border-r border-border bg-surface transition-[width] duration-200 ease-out lg:flex",
            collapsed ? "w-16" : "w-64",
          )}
        >
          <Link
            to="/"
            className={cn(
              "flex items-center py-4 transition hover:opacity-90 focus-ring",
              collapsed ? "justify-center px-2" : "gap-3 px-4",
            )}
            aria-label={t("nav.dashboard")}
            title={t("app.title")}
          >
            <span className={cn(ICON_WELL, "size-11 shrink-0 rounded-xl")}>
              <Heart size={18} fill="currentColor" />
            </span>
            {!collapsed ? (
              <span className="min-w-0 truncate text-xl font-bold text-text-bright">
                {t("app.title")}
              </span>
            ) : null}
          </Link>
          <nav
            className="flex min-h-0 flex-1 flex-col gap-1 overflow-y-auto p-2"
            aria-label={t("nav.main")}
          >
            {mode === "ap" ? (
              apNavItems.map((item) => (
                <NavItemLink
                  key={item.to}
                  item={item}
                  collapsed={collapsed}
                  updateAvailable={false}
                />
              ))
            ) : (
              <>
                <NavItemLink
                  item={homeItem}
                  collapsed={collapsed}
                  updateAvailable={updateAvailable}
                />
                <div className="space-y-1">
                  <div className="relative">
                    {collapsed ? (
                      <NavLink
                        to="/settings"
                        end
                        data-testid="settings-nav-link"
                        title={t("nav.settings")}
                        aria-label={t("nav.settings")}
                        className={({ isActive }) => cn(navClass(isActive, true), "w-full")}
                      >
                        <span className="relative inline-flex">
                          <Settings size={18} aria-hidden />
                          {updateAvailable ? (
                            <span
                              className="absolute -top-0.5 -right-0.5 size-2 rounded-full bg-warning"
                              aria-hidden
                            />
                          ) : null}
                        </span>
                      </NavLink>
                    ) : (
                      <>
                        <NavLink
                          to="/settings"
                          end
                          data-testid="settings-nav-link"
                          title={t("nav.settings")}
                          className={({ isActive }) =>
                            cn(navClass(isActive, false), "w-full pl-11")
                          }
                        >
                          <span className="flex min-w-0 flex-1 items-center gap-2">
                            <span className="truncate">{t("nav.settings")}</span>
                            {updateAvailable && !settingsOpen ? (
                              <span
                                className="size-2 shrink-0 rounded-full bg-warning"
                                aria-hidden
                              />
                            ) : null}
                          </span>
                          <Settings size={18} aria-hidden className="shrink-0" />
                        </NavLink>
                        <IconButton
                          type="button"
                          size="sm"
                          data-testid="settings-nav-toggle"
                          onClick={toggleSettingsGroup}
                          aria-expanded={settingsOpen}
                          aria-controls="settings-nav-group"
                          aria-label={
                            settingsOpen ? t("nav.settings-collapse") : t("nav.settings-expand")
                          }
                          className={cn(
                            "absolute top-1/2 left-1 -translate-y-1/2",
                            pathname === "/settings" ? "text-accent" : "text-muted",
                          )}
                        >
                          <ChevronRight
                            size={18}
                            aria-hidden
                            className={cn(
                              "transition-transform duration-200",
                              settingsOpen ? "rotate-90" : "",
                            )}
                          />
                        </IconButton>
                      </>
                    )}
                  </div>
                  {settingsOpen && !collapsed ? (
                    <div
                      id="settings-nav-group"
                      data-testid="settings-nav-group"
                      className="ml-3"
                      role="group"
                    >
                      {settingsChildren.map((item, index) => {
                        const isLast = index === settingsChildren.length - 1;
                        return (
                          <div key={item.to} className="relative pl-3">
                            <span
                              aria-hidden
                              className={cn(
                                "pointer-events-none absolute top-0 left-0 w-px bg-border",
                                isLast ? "h-1/2" : "bottom-0",
                              )}
                            />
                            <span
                              aria-hidden
                              className="pointer-events-none absolute top-1/2 left-0 h-px w-3 -translate-y-1/2 bg-border"
                            />
                            <NavItemLink
                              item={item}
                              collapsed={false}
                              updateAvailable={updateAvailable}
                            />
                          </div>
                        );
                      })}
                    </div>
                  ) : null}
                </div>
              </>
            )}
          </nav>
          <ShellFooter collapsed={collapsed} />
        </aside>
      ) : null}

      <div className="flex min-w-0 flex-1 flex-col lg:h-full lg:overflow-hidden">
        <header className="sticky top-0 z-30 shrink-0 border-b border-border bg-bg/95 pt-[max(0.75rem,env(safe-area-inset-top))] pb-3 backdrop-blur">
          <div className="flex w-full items-center gap-3 px-3 lg:px-4">
            {!hideChrome ? (
              <div className="hidden lg:contents">
                <IconButton
                  type="button"
                  variant="bordered"
                  data-testid="sidebar-collapse-toggle"
                  className="size-10 rounded-xl"
                  onClick={() => setCollapsed((prev) => !prev)}
                  aria-label={collapsed ? t("nav.expand") : t("nav.collapse")}
                  title={collapsed ? t("nav.expand") : t("nav.collapse")}
                  aria-expanded={!collapsed}
                >
                  {collapsed ? (
                    <PanelLeftOpen size={18} aria-hidden />
                  ) : (
                    <PanelLeftClose size={18} aria-hidden />
                  )}
                </IconButton>
              </div>
            ) : null}
            <Link
              to="/"
              data-testid="mobile-brand-home"
              className={cn(ICON_WELL, "size-10 shrink-0 rounded-xl lg:hidden")}
              aria-label={t("nav.dashboard")}
              title={t("app.title")}
            >
              <Heart size={16} fill="currentColor" aria-hidden />
            </Link>
            {settingsChild ? (
              <Link
                to="/settings"
                data-testid="settings-back"
                className={cn(
                  "inline-flex size-10 shrink-0 items-center justify-center rounded-xl border border-border bg-surface text-muted transition focus-ring lg:hidden",
                  HOVER_SURFACE,
                )}
                aria-label={t("nav.back")}
                title={t("nav.back")}
              >
                <ArrowLeft size={18} aria-hidden />
              </Link>
            ) : null}
            <h1 className="min-w-0 flex-1 truncate text-xl font-bold text-text-bright lg:text-2xl">
              {title}
            </h1>
          </div>
          <div className="mt-2 flex justify-end px-3 lg:hidden">
            <ShellUtilityButtons />
          </div>
          {live === "reconnecting" ? (
            <div className="mx-3 mt-2 lg:mx-4">
              <Alert variant="warning" className="rounded-lg px-3 py-2">
                <span className="font-medium text-warning">{t("status.live-reconnecting")}</span>
              </Alert>
            </div>
          ) : null}
        </header>

        <div className="min-h-0 flex-1 lg:overflow-y-auto">
          <main className="mx-auto w-full max-w-3xl px-4 py-4 pb-[max(4.5rem,calc(env(safe-area-inset-bottom)+3.5rem))] lg:px-6 lg:pb-8">
            {children}
          </main>
        </div>

        {!hideChrome ? (
          <nav
            className="fixed inset-x-0 bottom-0 z-30 border-t border-border bg-surface/95 px-2 pb-[max(0.35rem,env(safe-area-inset-bottom))] pt-1 backdrop-blur lg:hidden"
            aria-label={t("nav.main")}
          >
            {mode === "ap" ? (
              <div className="mx-auto flex max-w-3xl gap-1">
                {apNavItems.map((item) => (
                  <NavLink
                    key={item.to}
                    to={item.to}
                    end
                    className={({ isActive }) => mobileNavClass(isActive)}
                  >
                    <item.icon size={18} aria-hidden />
                    <span className="truncate">{t(item.labelKey)}</span>
                  </NavLink>
                ))}
              </div>
            ) : (
              <div className="mx-auto flex max-w-3xl gap-1">
                <NavLink
                  to="/"
                  end
                  title={t("nav.dashboard")}
                  aria-label={t("nav.dashboard")}
                  className={({ isActive }) => mobileNavClass(isActive)}
                >
                  <Heart size={18} aria-hidden />
                  <span className="truncate">{t("nav.dashboard")}</span>
                </NavLink>
                <NavLink
                  to="/settings"
                  end
                  title={t("nav.settings")}
                  aria-label={t("nav.settings")}
                  className={({ isActive }) => mobileNavClass(isActive || settingsActive)}
                >
                  <span className="relative inline-flex">
                    <Settings size={18} aria-hidden />
                    {updateAvailable ? (
                      <span
                        className="absolute -top-0.5 -right-0.5 size-2 rounded-full bg-warning"
                        aria-hidden
                      />
                    ) : null}
                  </span>
                  <span className="truncate">{t("nav.settings")}</span>
                </NavLink>
              </div>
            )}
          </nav>
        ) : null}
      </div>
    </div>
  );
}
