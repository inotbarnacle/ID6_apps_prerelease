#ifndef ID6_DIAGNOSTIC_GATEWAY_H
#define ID6_DIAGNOSTIC_GATEWAY_H

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define ID6_GATEWAY_SETTINGS "/var/opt/hmi/data/ID5/gateway-ip.txt"

static inline int diagnosticGatewayIPv4(const char* candidate) {
    struct in_addr address;
    if (inet_pton(AF_INET, candidate, &address) != 1) return 0;
    unsigned high = ntohl(address.s_addr) >> 24;
    return high && high != 127 && high < 224;
}

static inline int readDiagnosticGateway(const char* path, char* host) {
    FILE* file = fopen(path, "r");
    if (!file) return 0;
    char candidate[32], extra;
    int valid = fscanf(file, "%31s %c", candidate, &extra) == 1 &&
                diagnosticGatewayIPv4(candidate);
    if (valid) strcpy(host, candidate);
    fclose(file);
    return valid;
}

#ifdef __cplusplus
class DiagnosticGateway {
    char host_[16];

public:
    explicit DiagnosticGateway(const char* settings, const char* fallback = "auto") {
        strcpy(host_, fallback);
        if (!readDiagnosticGateway(ID6_GATEWAY_SETTINGS, host_))
            readDiagnosticGateway(settings, host_);
    }

    const char* host() const { return host_; }
};
#endif
#endif
