#pragma once
#include <cstdint>

enum class NtpServer : uint32_t { Pool=0, Cloudflare=1 };
constexpr bool ntp_server_valid(uint32_t value) { return value<=1; }
constexpr const char *ntp_primary(NtpServer server) {
    return server==NtpServer::Cloudflare ? "time.cloudflare.com" : "pool.ntp.org";
}
constexpr const char *ntp_secondary(NtpServer server) {
    return server==NtpServer::Cloudflare ? "pool.ntp.org" : "time.cloudflare.com";
}
