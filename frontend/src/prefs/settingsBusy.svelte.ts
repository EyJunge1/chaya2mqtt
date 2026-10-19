class SettingsBusy {
  value = $state(false);
}

export const settingsUiBusy = new SettingsBusy();

export function setSettingsUiBusy(busy: boolean): void {
  settingsUiBusy.value = busy;
}
