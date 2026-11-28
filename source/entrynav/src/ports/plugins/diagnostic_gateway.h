#ifndef PORT_DIAGNOSTIC_GATEWAY_H
#define PORT_DIAGNOSTIC_GATEWAY_H

#include "diagnostic_worker.h"

class DiagnosticGateway {
    char host_[16];
public:
    explicit DiagnosticGateway(const char*) {
        if (!installed_gateway(host_)) strcpy(host_,"auto");
    }
    const char *host() const { return host_; }
};

#endif
