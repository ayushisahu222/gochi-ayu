// wifi_creds.h — WiFi credentials from compile-time -D flags.
//
// Shared by the clock (NTP sync) and weather (HTTP fetch) modules so the
// SSID/password stringizing lives in one place. The Makefile feeds bare
// tokens (e.g. -DWIFI_SSID=mynet) from the gitignored .env; STRINGIZE
// turns them into C string literals. With no flags both resolve to "",
// and the dependent feature stays dormant.
#pragma once

#define WIFI_CREDS_STRINGIZE_(x) #x
#define WIFI_CREDS_STRINGIZE(x) WIFI_CREDS_STRINGIZE_(x)

#ifdef WIFI_SSID
#define WIFI_SSID_STR WIFI_CREDS_STRINGIZE(WIFI_SSID)
#else
#define WIFI_SSID_STR ""
#endif

#ifdef WIFI_PASS
#define WIFI_PASS_STR WIFI_CREDS_STRINGIZE(WIFI_PASS)
#else
#define WIFI_PASS_STR ""
#endif

// True when an SSID was provided at build time.
inline bool wifiHasCreds() { return WIFI_SSID_STR[0] != '\0'; }
