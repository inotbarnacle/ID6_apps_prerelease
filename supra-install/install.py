#!/usr/bin/env python3
"""Install BoostDeck, ShiftDeck, Engine Fault Codes, and Bootmod3 Suite.

Run this only after Python 3.12 and paramiko 2.12.0 are installed, and only
while the ENET cable is plugged into the car and this computer. The script
sends the car network out the ENET adapter, checks that this Supra uses
gateway 169.254.38.178, waits until SSH is enabled, then copies the apps.
"""
from __future__ import print_function

import hashlib
import os
import platform
import socket
import subprocess
import sys
import time

HEAD = "169.254.199.99"
GATEWAY = "169.254.38.178"
PASSWORD = "ts&SK412"
PLUGIN_ROOT = "/var/opt/hmi/data/ID5/Plugins"
HERE = os.path.dirname(os.path.abspath(__file__))
PAYLOAD = os.path.join(HERE, "payload")
APPS = ("BoostDeck", "ShiftDeck", "EngineCodes", "Bootmod3Suite")


def die(message):
    print("\n" + message)
    sys.exit(1)


def require_python():
    if sys.version_info[:2] != (3, 12):
        die("This installer needs Python 3.12. You are running %s." % sys.version.split()[0])
    try:
        import paramiko  # noqa: F401
    except ImportError:
        die("Paramiko is not installed. From this folder run:\n"
            "  python3 -m pip install paramiko==2.12.0")
    import paramiko
    if not paramiko.__version__.startswith("2.12."):
        die("Paramiko %s is installed. Remove it and install 2.12.0:\n"
            "  python3 -m pip install paramiko==2.12.0" % paramiko.__version__)


def run(args, check=False):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          universal_newlines=True, check=check)


def mac_interfaces():
    default = ""
    info = run(["route", "-n", "get", "default"])
    for line in info.stdout.splitlines():
        if "interface:" in line:
            default = line.split()[-1]
    found = []
    names = run(["ifconfig", "-l"]).stdout.split()
    for name in names:
        if name.startswith(("lo", "awdl", "llw", "utun", "bridge", "gif", "stf")):
            continue
        addr = run(["ipconfig", "getifaddr", name])
        ip = addr.stdout.strip()
        if ip.startswith("169.254."):
            found.append((name, ip, name == default))
    return found


def windows_interfaces():
    ps = (
        "Get-NetIPAddress -AddressFamily IPv4 | "
        "Where-Object { $_.IPAddress -like '169.254.*' } | "
        "ForEach-Object { '{0}|{1}' -f $_.InterfaceIndex, $_.IPAddress }"
    )
    listed = run(["powershell", "-NoProfile", "-Command", ps])
    found = []
    for line in listed.stdout.splitlines():
        if "|" not in line:
            continue
        index, ip = line.strip().split("|", 1)
        found.append((index.strip(), ip.strip()))
    return found


def choose_adapter():
    system = platform.system()
    if system == "Darwin":
        found = mac_interfaces()
        usable = [item for item in found if not item[2]]
        if not usable and found:
            usable = found
        if len(usable) != 1:
            die("Plug the ENET cable into this Mac and the car, wait 10 seconds, "
                "and run this again. Expected one 169.254 adapter. Found: %s" % (found,))
        return {"os": "mac", "name": usable[0][0], "ip": usable[0][1]}
    if system == "Windows":
        if not ctypes_is_admin():
            die("Close this window. Open the Start menu, type PowerShell, "
                "right-click Windows PowerShell, and choose Run as administrator. "
                "Then cd to this folder and run:\n  py -3.12 install.py")
        found = windows_interfaces()
        if len(found) != 1:
            die("Plug the ENET cable into this PC and the car, wait 10 seconds, "
                "and run this again. Expected one 169.254 adapter. Found: %s" % (found,))
        return {"os": "windows", "index": found[0][0], "ip": found[0][1]}
    die("This installer supports macOS and Windows only.")


def ctypes_is_admin():
    try:
        import ctypes
        return bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:
        return False


def fix_route(adapter):
    print("The car network will use the ENET adapter. Wi-Fi stays your internet.")
    if adapter["os"] == "mac":
        print("Your Mac will ask for your Mac login password. Nothing appears as you type. Press Return.")
        print("That password is for this Mac. It is not a car password.")
        for args in (
            ["sudo", "route", "-n", "delete", "-host", "169.254.255.255"],
            ["sudo", "route", "-n", "delete", "-host", HEAD],
            ["sudo", "route", "-n", "delete", "-net", "169.254.0.0", "-netmask", "255.255.0.0"],
        ):
            run(args)
        added = run(["sudo", "route", "-n", "add", "-net", "169.254.0.0",
                     "-netmask", "255.255.0.0", "-interface", adapter["name"]])
        if added.returncode != 0 and "File exists" not in added.stderr:
            die("Could not add the car route:\n%s" % added.stderr.strip())
        check = run(["route", "-n", "get", HEAD])
        if adapter["name"] not in check.stdout:
            die("The car address is still not using %s.\n%s" % (adapter["name"], check.stdout))
        wifi = run(["route", "-n", "get", "default"])
        if adapter["name"] in wifi.stdout:
            die("The default internet route moved onto the ENET adapter. Unplug the cable and stop.")
        print("Car traffic is using %s (%s)." % (adapter["name"], adapter["ip"]))
        return
    for destination in ("169.254.255.255", HEAD, "169.254.0.0"):
        run(["route", "delete", destination])
    ps = (
        "$idx = %s; "
        "Get-NetRoute -DestinationPrefix '169.254.0.0/16' -ErrorAction SilentlyContinue | "
        "Where-Object { $_.InterfaceIndex -ne $idx } | Remove-NetRoute -Confirm:$false; "
        "$have = Get-NetRoute -DestinationPrefix '169.254.0.0/16' -ErrorAction SilentlyContinue | "
        "Where-Object { $_.InterfaceIndex -eq $idx }; "
        "if (-not $have) { New-NetRoute -DestinationPrefix '169.254.0.0/16' -InterfaceIndex $idx "
        "-NextHop '0.0.0.0' -RouteMetric 1 -PolicyStore ActiveStore | Out-Null }; "
        "(Find-NetRoute -RemoteIPAddress '%s').InterfaceIndex"
    ) % (adapter["index"], HEAD)
    checked = run(["powershell", "-NoProfile", "-Command", ps])
    if adapter["index"] not in checked.stdout.split():
        die("The car address is still not using the ENET adapter.\n%s\n%s"
            % (checked.stdout, checked.stderr))
    print("Car traffic is using interface %s (%s)." % (adapter["index"], adapter["ip"]))


def discover(adapter):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.settimeout(2)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        sock.bind((adapter["ip"], 0))
        sock.sendto(bytes.fromhex("000000000011"), ("169.254.255.255", 6811))
        replies = []
        deadline = time.time() + 3
        while time.time() < deadline:
            try:
                data, addr = sock.recvfrom(2048)
            except socket.timeout:
                break
            replies.append((addr[0], data))
    finally:
        sock.close()
    if not replies:
        die("The car did not answer. Leave the cable plugged in, make sure the iDrive screen is on, "
            "and run this again.")
    blob = b"".join(item[1] for item in replies)
    text = blob.decode("latin1", "replace")
    print("The car answered from: %s" % ", ".join(sorted(set(item[0] for item in replies))))
    senders = {item[0] for item in replies}
    if GATEWAY not in senders and GATEWAY.encode("ascii") not in blob and GATEWAY not in text:
        die("This package only works when the car gateway is %s.\n"
            "The reply did not contain that address, so nothing was copied.\n%s"
            % (GATEWAY, text[:400]))
    print("Gateway %s is present." % GATEWAY)


def wait_for_ssh():
    print("\nLeave the ENET cable plugged in.")
    print("Open Bavarian SSH Client. Leave the IP box blank. Press Enable SSH once.")
    print("You do not type a password into Bavarian.")
    print("When it says SSH is enabled, come back here and press Return.")
    try:
        input()
    except EOFError:
        pass
    for _ in range(30):
        probe = socket.socket()
        probe.settimeout(2)
        try:
            probe.connect((HEAD, 22))
            probe.close()
            print("SSH is open on %s." % HEAD)
            return
        except Exception:
            probe.close()
            time.sleep(1)
    die("SSH did not open on %s. In Bavarian, press Enable SSH once more, then run this again." % HEAD)


def connect():
    import paramiko
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HEAD, username="root", password=PASSWORD, timeout=12,
                   banner_timeout=20, auth_timeout=20,
                   allow_agent=False, look_for_keys=False)
    return client


def remote(client, command, payload=None):
    stdin, stdout, stderr = client.exec_command(command, timeout=90)
    if payload is not None:
        stdin.write(payload)
        stdin.flush()
    stdin.channel.shutdown_write()
    out = stdout.read()
    err = stderr.read()
    code = stdout.channel.recv_exit_status()
    return code, out, err


def mode_for(name):
    if name.endswith((".xml", ".manifest", ".zip")):
        return "644"
    return "755"


def files_to_copy():
    items = []
    for app in APPS:
        folder = os.path.join(PAYLOAD, app)
        if not os.path.isdir(folder):
            die("Missing payload folder: %s" % folder)
        for name in sorted(os.listdir(folder)):
            path = os.path.join(folder, name)
            if os.path.isfile(path):
                items.append((app, name, path))
    if len(items) != 24:
        die("Expected 24 payload files, found %d." % len(items))
    return items


def upload_all(client):
    items = files_to_copy()
    for app in APPS:
        code, out, err = remote(
            client,
            "on -f /net/hu-omap /bin/mkdir -p %s/%s && echo DIR_OK" % (PLUGIN_ROOT, app))
        if code != 0 or b"DIR_OK" not in out:
            die("Could not create %s: %s" % (app, err.decode("latin1", "replace")[-300:]))
    for index, (app, name, path) in enumerate(items, 1):
        data = open(path, "rb").read()
        dest = "%s/%s/%s" % (PLUGIN_ROOT, app, name)
        mode = mode_for(name)
        command = ("on -f /net/hu-omap /bin/ksh -c '"
                   "/bin/dd of=%s.new bs=65536 >/dev/null 2>&1 && "
                   "/bin/rm -f %s && /bin/mv %s.new %s && "
                   "/bin/chmod %s %s && echo UP_OK'" % (dest, dest, dest, dest, mode, dest))
        print("UPLOAD %d/%d %s/%s (%d bytes)" % (index, len(items), app, name, len(data)))
        code, out, err = remote(client, command, data)
        if code != 0 or b"UP_OK" not in out:
            die("Upload failed for %s/%s\n%s" % (app, name, err.decode("latin1", "replace")[-400:]))
        code, out, err = remote(
            client,
            "on -f /net/hu-omap /bin/ksh -c 'n=$(/bin/wc -c < \"%s\"); "
            "echo \"FILE $n\"; /bin/cat \"%s\"; echo ENDREAD'" % (dest, dest))
        if code != 0 or not out.startswith(b"FILE "):
            die("Readback failed for %s/%s" % (app, name))
        line, _, rest = out.partition(b"\n")
        size = int(line.split()[1])
        got = rest[:size]
        if hashlib.sha256(got).digest() != hashlib.sha256(data).digest() or not rest[size:].startswith(b"ENDREAD"):
            die("The copied file does not match this computer: %s/%s" % (app, name))
        print("OK %s/%s" % (app, name))


def main():
    print("Do not unplug the ENET cable while this is running.")
    require_python()
    adapter = choose_adapter()
    print("ENET adapter address is %s." % adapter["ip"])
    fix_route(adapter)
    discover(adapter)
    wait_for_ssh()
    print("Copying four apps. Leave the cable plugged in.")
    client = connect()
    try:
        upload_all(client)
    finally:
        client.close()
    print("\nCopy finished and every file matched.")
    print("Unplug the ENET cable now.")
    print("Stay parked. Hold the dashboard volume knob until the screen goes completely dark.")
    print("When iDrive comes back, open My Vehicle. You should see BoostDeck, ShiftDeck, "
          "Engine Fault Codes, and Bootmod3 Suite.")


if __name__ == "__main__":
    main()
