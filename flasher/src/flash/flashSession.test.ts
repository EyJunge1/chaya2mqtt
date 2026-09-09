import { describe, expect, it } from "vitest";
import {
  isChannelSwitchLocked,
  isFlashJobLocked,
  nextChannelSelection,
  shouldMountFlashDialog,
} from "./flashSession";

describe("flashSession", () => {
  it("locks channel radios while connecting or flashing", () => {
    expect(isChannelSwitchLocked(false, false)).toBe(false);
    expect(isChannelSwitchLocked(true, false)).toBe(true);
    expect(isChannelSwitchLocked(false, true)).toBe(true);
    expect(isChannelSwitchLocked(true, true)).toBe(true);
    expect(isChannelSwitchLocked(false, false, true)).toBe(true);
  });

  it("blocks a new flash job until the previous one is settled (BUG-FE-05)", () => {
    expect(isFlashJobLocked(false, false, false)).toBe(false);
    expect(isFlashJobLocked(true, false, false)).toBe(true);
    expect(isFlashJobLocked(false, true, false)).toBe(true);
    expect(isFlashJobLocked(false, false, true)).toBe(true);
  });

  it("ignores channel clicks while locked so the selected manifest cannot change", () => {
    expect(nextChannelSelection("stable", "beta", false)).toBe("beta");
    expect(nextChannelSelection("stable", "beta", true)).toBe("stable");
    expect(nextChannelSelection("beta", "stable", true)).toBe("beta");
  });

  it("keeps FlashDialog mounted during flash even if the selected channel has no manifest", () => {
    expect(shouldMountFlashDialog(true, false)).toBe(true);
    expect(shouldMountFlashDialog(false, true)).toBe(true);
    expect(shouldMountFlashDialog(true, true)).toBe(true);
    expect(shouldMountFlashDialog(false, false)).toBe(false);
  });
});
