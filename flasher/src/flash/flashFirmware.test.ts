import { describe, expect, it } from "vitest";
import { firmwareFetchInit } from "./flashFetch";
import { emitAfterFlashCleanup } from "./flashFirmware";

describe("flashFirmware fetch cache", () => {
  it("requests firmware parts without HTTP cache (RC-FE-04)", () => {
    expect(firmwareFetchInit()).toEqual({ cache: "no-store" });
  });
});

describe("flashFirmware cleanup (BUG-FE-05)", () => {
  it("emits terminal error only after disconnect settles", async () => {
    const order: string[] = [];
    await emitAfterFlashCleanup(
      async () => {
        order.push("disconnect");
      },
      () => {
        order.push("error");
      },
    );
    expect(order).toEqual(["disconnect", "error"]);
  });

  it("still emits after cleanup throws so the UI can settle the job", async () => {
    const order: string[] = [];
    await emitAfterFlashCleanup(
      async () => {
        order.push("disconnect");
        throw new Error("already closed");
      },
      () => {
        order.push("error");
      },
    );
    expect(order).toEqual(["disconnect", "error"]);
  });
});
