/// <reference types="vitest/config" />
import type { Plugin } from "vite";
import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";
import tailwindcss from "@tailwindcss/vite";
import { mockDevicePlugin } from "./mock/mockPlugin.ts";

/**
 * Tailwind Preflight sets `-webkit-text-size-adjust: 100%`. Firefox accepts only
 * `none`/`auto` for that property and logs a console error for percentage values.
 * `none` still prevents iOS orientation font inflation.
 */
function fixFirefoxTextSizeAdjust(): Plugin {
  const rewrite = (code: string) =>
    code.replace(/-webkit-text-size-adjust:\s*100%/g, "-webkit-text-size-adjust: none");

  return {
    name: "fix-firefox-text-size-adjust",
    enforce: "post",
    transform(code) {
      if (!code.includes("text-size-adjust")) return;
      const next = rewrite(code);
      return next === code ? undefined : next;
    },
    generateBundle(_options, bundle) {
      for (const item of Object.values(bundle)) {
        if (item.type !== "asset" || !item.fileName.endsWith(".css")) continue;
        if (typeof item.source === "string") {
          item.source = rewrite(item.source);
        } else {
          item.source = rewrite(Buffer.from(item.source).toString("utf8"));
        }
      }
    },
  };
}

function makeBuiltHtmlCaptiveCompatible(): Plugin {
  return {
    name: "make-built-html-captive-compatible",
    transformIndexHtml(html) {
      return html
        .replaceAll(" crossorigin", "")
        .replace(
          /<script type="module" src="(\/assets\/[^"]+\.js)"><\/script>/g,
          '<script defer src="$1"></script>',
        );
    },
  };
}

export default defineConfig({
  plugins: [
    svelte(),
    tailwindcss(),
    fixFirefoxTextSizeAdjust(),
    makeBuiltHtmlCaptiveCompatible(),
    mockDevicePlugin(),
  ],
  server: {
    host: "127.0.0.1",
    port: 5173,
  },
  build: {
    outDir: "dist",
    assetsDir: "assets",
    cssCodeSplit: false,
    sourcemap: false,
    // Hashed filenames enable immutable caching; blob stores gzip without .gz URLs.
    rollupOptions: {
      output: {
        entryFileNames: "assets/[name]-[hash].js",
        chunkFileNames: "assets/[name]-[hash].js",
        assetFileNames: "assets/[name]-[hash][extname]",
      },
    },
  },
  resolve: process.env.VITEST
    ? {
        conditions: ["browser"],
      }
    : undefined,
  test: {
    environment: "jsdom",
    globals: true,
    setupFiles: "./src/test/setup.ts",
    include: ["src/**/*.test.ts", "mock/**/*.test.ts"],
    coverage: {
      provider: "v8",
      reporter: ["text", "html", "lcov"],
      reportsDirectory: "./coverage",
      include: ["src/**/*.{ts,svelte}"],
      exclude: [
        "src/main.ts",
        "src/**/*.test.ts",
        "src/**/*.test.svelte",
        "src/test/**",
        "src/api/types.ts",
        "src/**/*.d.ts",
      ],
      thresholds: {
        lines: 70,
        functions: 70,
        statements: 70,
        branches: 60,
      },
    },
  },
});
