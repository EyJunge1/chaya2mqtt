import { NavCard } from "../components/Card";
import { useI18n } from "../i18n/useI18n";
import { settingsNavItems } from "../nav/settingsNav";

export function SettingsOverviewPage() {
  const { t } = useI18n();

  return (
    <div className="grid gap-3 sm:grid-cols-2">
      {settingsNavItems.map((item) => (
        <NavCard
          key={item.to}
          to={item.to}
          title={t(item.labelKey)}
          subtitle={t(item.subtitleKey)}
          icon={item.icon}
        />
      ))}
    </div>
  );
}
