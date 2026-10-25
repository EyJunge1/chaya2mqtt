#pragma once

/** Local SemVer without leading 'v' (e.g. 1.0.0 or 1.0.0-rc.1). Use "dev" until a release.
 *  Release CI checks this string equals the Git tag (minus `v`). Keep on one line.
 */
inline constexpr const char kAppVersion[] = "0.1.0";
