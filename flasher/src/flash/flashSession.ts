/** True while a flash session still owns the serial port (open, connecting, or not settled). */
export function isFlashJobLocked(
  flashOpen: boolean,
  connecting: boolean,
  jobActive: boolean,
): boolean {
  return flashOpen || connecting || jobActive;
}

/** Channel radios must stay frozen while a port is opening or a flash is open. */
export function isChannelSwitchLocked(
  flashOpen: boolean,
  connecting: boolean,
  jobActive = false,
): boolean {
  return isFlashJobLocked(flashOpen, connecting, jobActive);
}

export function nextChannelSelection<C>(current: C, next: C, locked: boolean): C {
  return locked ? current : next;
}

/** Keep FlashDialog mounted for the whole flash session, even without a selected manifest. */
export function shouldMountFlashDialog(hasSelectedInfo: boolean, flashOpen: boolean): boolean {
  return hasSelectedInfo || flashOpen;
}
