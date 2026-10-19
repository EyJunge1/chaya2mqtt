#pragma once

/** Local firmware version without leading 'v'. Bump by hand before tagging a release.
 *  Release CI checks this string equals the Git tag (minus `v`). Keep on one line.
 */
inline constexpr const char kAppVersion[] = "dev";
