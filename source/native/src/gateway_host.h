#ifndef GATEWAY_HOST_H
#define GATEWAY_HOST_H
/* Reference cars reach the diagnostic gateway at 169.254.21.3.
   Set GATEWAY_HOST at compile time when a head unit uses another address. */
#ifndef GATEWAY_HOST
#define GATEWAY_HOST "169.254.21.3"
#endif
#endif
