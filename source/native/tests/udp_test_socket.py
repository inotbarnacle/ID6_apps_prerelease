"""Private telemetry receivers for host tests, separate from project IPC roles."""
import socket


def telemetry_socket():
    """Return a bound loopback UDP socket; the caller owns and closes it.

    Port zero can select a project port. In particular, occupying 42870 tells
    a worker that a HUD is present and makes it publish binary HUD samples.
    Keep private receivers outside 42860..42879, while role-specific tests
    continue to bind their explicit HUD/lease/activity ports themselves.
    """
    for _ in range(32):
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            udp.bind(("127.0.0.1", 0))
            if not 42860 <= udp.getsockname()[1] <= 42879:
                return udp
        except BaseException:
            udp.close()
            raise
        udp.close()
    raise OSError("could not allocate telemetry receiver outside project IPC ports")
