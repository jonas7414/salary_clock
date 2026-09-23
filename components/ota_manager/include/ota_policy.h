#pragma once
#include <cstddef>
#include <cstdint>

namespace ota {
constexpr size_t VERSION_CAPACITY=32, URL_CAPACITY=512, MAX_RELEASE_JSON=32768;
struct Version {
    uint32_t major{},minor{},patch{};
    char prerelease[VERSION_CAPACITY]{};
};
bool parse_version(const char *text,Version &out);
int compare_versions(const Version &left,const Version &right);
bool https_url_allowed(const char *url);
bool parse_sha256(const char *text,uint8_t output[32]);
bool parse_checksum_file(const char *text,size_t length,const char *asset,uint8_t output[32]);
struct Release {
    char version[VERSION_CAPACITY]{};
    char firmware_url[URL_CAPACITY]{};
    char checksum_url[URL_CAPACITY]{};
    uint8_t sha256[32]{};
    uint32_t size{};
    bool has_sha256{};
};
bool parse_release(const char *json,size_t length,const char *repository,const char *asset,
                   Release &out,const char **reason);
}
