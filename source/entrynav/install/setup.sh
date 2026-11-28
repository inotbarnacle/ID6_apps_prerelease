#!/bin/sh
# Checks, installs, updates and removes one EntryNav-Apps plugin (BoostDeck, ShiftDeck, Bootmod3 Suite, MHD+ Suite or
# Engine Fault Codes) on an EntryNav2, with the car's bash 3.2 and BusyBox 1.21. Each plugin is installed on its own; Flare Drive
# runs the same commands.
#
#   sh setup.sh check                every check of the installation; writes nothing
#   sh setup.sh install              the same checks, then the installation, if and only if they all pass
#   sh setup.sh status               what is installed and in what state; writes nothing but its log
#   sh setup.sh gateway [IP]         finds the diagnostic gateway (or checks IP) and saves it for the apps
#   sh setup.sh tidy                 deletes what an earlier operation had to leave until a restart
#   sh setup.sh uninstall [--purge]  removes everything it installed; --purge also deletes its settings
#
# check and install run from the unpacked release archive, in /tmp. The installation puts this script in the app's
# folder, from where the other commands work without the archive.
#
# What is written to the system partition, and how:
#   - the app's folder in /opt/hmi/data/ro/Plugins (marked with .installed-by-entrynav-apps), which holds the app, its
#     worker, ena-gateway and this script, and /etc/firewall/entrynav-<id>.rules (its first line marks it), which BMW's
#     iptables-firewall runs at start-up to let the app reach the gateway on TCP 6801. Nothing of BMW's is changed;
#   - only once every check has passed; the partition is read-only before, and goes back to read-only within seconds;
#   - the new version is first copied to a working folder (/opt/.entrynav-<id>.new) and checked against MD5SUMS. The
#     app's folder only changes by renaming a whole folder, so a power cut leaves the previous or the new version;
#   - the folder it replaces (/opt/.entrynav-<id>.old) is deleted afterwards, unless iDrive still has the app loaded:
#     deleting it would keep the partition writable. tidy deletes it after a restart;
#   - hang-ups and Ctrl-C are ignored while the partition is writable, and leaving always makes it read-only again.
# The gateway's address, shared by the apps, and their settings live in /var/local/data/EntryNavApps, on the data
# partition. Each run is logged to /tmp/entrynav-apps-setup.log.

PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
umask 022

# ---- this plugin (tools/package_release.py fills these in) -------------------------------------------------------------
ID=@ID@
APP=@APP@
NAME="@NAME@"
FORMER_NAME="@FORMER_NAME@"  # its name before a rename, or empty
WORKER=@WORKER@
SETTINGS="@SETTINGS@"
# -----------------------------------------------------------------------------------------------------------------------

PLUGINS=/opt/hmi/data/ro/Plugins
TARGET=$PLUGINS/$APP
NEW=/opt/.entrynav-$ID.new
OLD=/opt/.entrynav-$ID.old
SOURCE=opt/hmi/data/ro/Plugins/$APP
# Every EntryNav-Apps plugin's folder name: the gateway's address is theirs to share.
SIBLINGS="BoostDeck ShiftDeck Bm3Maps MhdMaps EngineCodes WarmDeck LaunchDeck HybridDeck RoadJet BEAMer XDriveControl MgFlasher"
MARK=.installed-by-entrynav-apps
RULES_DIR=/etc/firewall
RULES=$RULES_DIR/entrynav-$ID.rules
RULES_NEW=$RULES_DIR/.entrynav-$ID.rules.new
RULES_MARK="# EntryNav-Apps $NAME: lets it reach the car's diagnostic gateway."
FORMER_RULES_MARK="# EntryNav-Apps $FORMER_NAME: lets it reach the car's diagnostic gateway."
FIREWALL=/usr/local/bin/iptables-firewall
DATA=/var/local/data/EntryNavApps
GATEWAY_FILE=$DATA/gateway-ip.txt
LOG=/tmp/entrynav-apps-setup.log
SELF_COPY=/tmp/entrynav-$ID-setup.sh
KEPT_COPY=$DATA/$ID-setup.sh
LOADER=/lib/ld-linux-armhf.so.3
LIBS=/opt/hmi/lib:/usr/local/lib:/usr/lib:/lib
MARGIN_KB=1024
NEEDED_COMMANDS="cat cp date df du grep id iptables ls md5sum mkdir mount mv rm sed sync"
# File systems on which a rename survives a power cut whole (journalled or log-structured).
SAFE_FILESYSTEMS="ext3 ext4 ubifs jffs2 yaffs yaffs2 f2fs btrfs xfs"
# Firewall rules, as iptables arguments, the ones the rules file makes at start-up: HSFZ out, and the gateway search
# out and its answers in (the gateway answers a broadcast from its own address, which the ESTABLISHED rule does not
# match). The search stays open because the apps search again by themselves when the saved address stops answering.
# They name vbr0, the head unit's car-side Ethernet, not an address: the gateway is usually link-local, but takes an
# address from tethering on the ENET cable and keeps it for a while after the cable comes out.
RULE_HSFZ="OUTPUT -o vbr0 -p tcp --dport 6801 -j ACCEPT"
RULE_IDENT_OUT="OUTPUT -o vbr0 -p udp --dport 6811 -j ACCEPT"
RULE_IDENT_IN="INPUT -i vbr0 -p udp --sport 6811 -j ACCEPT"
# The rule 0.1.0-test.1 installed. Removed with the new one, so an update leaves no stale copy after the restart.
RULE_HSFZ_OLD="OUTPUT -d 169.254.0.0/16 -p tcp --dport 6801 -j ACCEPT"

STOPS=0
WARNS=0
WRITABLE=0

say() {
    printf '%s\n' "$*" 2>/dev/null
    printf '%s\n' "$*" >>"$LOG" 2>/dev/null
}
ok() { say "  ok    $*"; }
info() { say "  info  $*"; }
warn() {
    say "  warn  $*"
    WARNS=$((WARNS + 1))
}
stop() {
    say "  STOP  $*"
    STOPS=$((STOPS + 1))
}

usage() {
    cat <<'EOF'
Usage:
  sh setup.sh check                every check of the installation; writes nothing
  sh setup.sh install              the checks, then the installation if they all pass
  sh setup.sh status               what is installed and in what state
  sh setup.sh gateway [IP]         finds the diagnostic gateway (or checks IP) and saves it
  sh setup.sh tidy                 deletes what an earlier operation left until a restart
  sh setup.sh uninstall [--purge]  removes it; --purge also deletes its settings
EOF
}

# ---- reading the system -----------------------------------------------------------------------------------------------
# Helpers name their variables _like_this: shell variables are global.

firmware() {
    _fw=
    if [ -r /etc/version ]; then
        read -r _fw _rest </etc/version
    fi
    printf '%s\n' "$_fw"
}

# The mount point holding a path: the longest mount point of /proc/mounts that is a prefix of it.
mount_of() {
    _best=/
    while read -r _dev _mnt _rest; do
        case $1 in
            "$_mnt" | "$_mnt"/*)
                if [ ${#_mnt} -gt ${#_best} ]; then
                    _best=$_mnt
                fi
                ;;
        esac
    done </proc/mounts
    printf '%s\n' "$_best"
}

# Options or type of the file system mounted on / (the last line for /, in effect).
root_field() {
    _value=
    while read -r _dev _mnt _type _o _rest; do
        if [ "$_mnt" = / ]; then
            if [ "$1" = type ]; then _value=$_type; else _value=$_o; fi
        fi
    done </proc/mounts
    printf '%s\n' "$_value"
}

root_read_only() {
    case ,$(root_field options), in
        *,ro,*) return 0 ;;
    esac
    return 1
}

data_mounted() {
    [ "$(mount_of /var/local/data)" = /var/local ]
}

installed() {
    [ -d "$TARGET" ] && [ ! -L "$TARGET" ] && [ -f "$TARGET/$MARK" ]
}

# Whether another EntryNav-Apps plugin is installed.
sibling_installed() {
    for _s in $SIBLINGS; do
        if [ "$_s" != "$APP" ] && [ -f "$PLUGINS/$_s/$MARK" ]; then
            return 0
        fi
    done
    return 1
}

# A folder this script may replace or delete: missing, or a real folder carrying the mark.
ours_or_missing() {
    if [ ! -e "$1" ] && [ ! -L "$1" ]; then
        return 0
    fi
    [ -d "$1" ] && [ ! -L "$1" ] && [ -f "$1/$MARK" ]
}

# A rules file this script may replace or delete: missing, or a real file whose first line is the mark, or the mark
# with the plugin's former name (an install from before the rename).
rules_ours_or_missing() {
    if [ ! -e "$1" ] && [ ! -L "$1" ]; then
        return 0
    fi
    [ -f "$1" ] && [ ! -L "$1" ] || return 1
    _first=$(sed -n 1p "$1")
    [ "$_first" = "$RULES_MARK" ] || { [ -n "$FORMER_NAME" ] && [ "$_first" = "$FORMER_RULES_MARK" ]; }
}

# Whether a running program maps, holds open or stands in a folder.
in_use() {
    for _map in /proc/[0-9]*/maps; do
        if [ -r "$_map" ] && grep -qF "$1/" "$_map" 2>/dev/null; then
            return 0
        fi
    done
    for _fds in /proc/[0-9]*/fd; do
        # One ls per process rather than one readlink per open file.
        # shellcheck disable=SC2010 # /proc names are numbers
        if [ -d "$_fds" ] && ls -l "$_fds" 2>/dev/null | grep -qF -- "-> $1/"; then
            return 0
        fi
    done
    # shellcheck disable=SC2010 # /proc names are numbers
    if ls -ld /proc/[0-9]*/cwd 2>/dev/null | grep -q -e "-> $1/" -e "-> $1\$"; then
        return 0
    fi
    return 1
}

leftovers() {
    for _d in "$NEW" "$OLD"; do
        if [ -e "$_d" ] || [ -L "$_d" ]; then
            printf '%s\n' "$_d"
        fi
    done
}

# Checks the files of a folder against the lines of an MD5SUMS for its path in the archive.
verify_dir() {
    _list=/tmp/entrynav-$ID-sums.$$
    sed -n "s#^\([0-9a-f]\{32\}\)  $2/#\1  #p" "$1" >"$_list" 2>/dev/null
    if [ ! -s "$_list" ]; then
        rm -f "$_list"
        return 1
    fi
    (cd "$3" && md5sum -c "$_list" >/dev/null 2>>"$LOG")
    _rc=$?
    rm -f "$_list"
    return $_rc
}

# ---- checks -----------------------------------------------------------------------------------------------------------
check_basics() {
    if [ "$(id -u 2>/dev/null)" != 0 ]; then
        stop "run as root"
    fi
    _missing=
    for _c in $NEEDED_COMMANDS; do
        command -v "$_c" >/dev/null 2>&1 || _missing="$_missing $_c"
    done
    if [ -n "$_missing" ]; then
        stop "commands missing:$_missing"
    fi
}

check_archive() {
    for _f in VERSION MD5SUMS plugin.json frameworks.md5 setup.sh "etc/firewall/entrynav-$ID.rules"; do
        if [ ! -f "$SRC/$_f" ]; then
            stop "the release is incomplete: $_f is missing (unpack the whole archive)"
            return 1
        fi
    done
    if [ ! -d "$SRC/$SOURCE" ]; then
        stop "the release is incomplete: $SOURCE is missing (unpack the whole archive)"
        return 1
    fi
    if (cd "$SRC" && md5sum -c MD5SUMS >/dev/null 2>>"$LOG"); then
        ok "$NAME $(cat "$SRC/VERSION"): every file matches MD5SUMS"
    else
        stop "the release is damaged: a file does not match MD5SUMS (download it again)"
        return 1
    fi
    if [ "$(sed -n 1p "$SRC/etc/firewall/entrynav-$ID.rules")" != "$RULES_MARK" ]; then
        stop "the release's firewall rule file does not carry $NAME's mark"
        return 1
    fi
    return 0
}

check_layout() {
    if root_read_only; then
        ok "system partition read-only"
    else
        stop "the system partition is writable right now ($(root_field options)): restart the unit first (reboot)"
    fi
    _type=$(root_field type)
    case " $SAFE_FILESYSTEMS " in
        *" $_type "*) ok "system partition is $_type: a rename survives a power cut whole" ;;
        *) stop "system partition is ${_type:-unknown}: a power cut could break a rename there" ;;
    esac
    for _p in /opt "$PLUGINS" "$RULES_DIR"; do
        if [ ! -d "$_p" ]; then
            stop "$_p is missing: not an EntryNav2"
        elif [ "$(mount_of "$_p")" != / ]; then
            stop "$_p is not on the system partition: this layout is not supported"
        fi
    done
    if [ -f "$FIREWALL" ] && grep -q 'config_dir="/etc/firewall"' "$FIREWALL" 2>/dev/null; then
        ok "the firewall runs the rules in $RULES_DIR at start-up"
    else
        stop "$FIREWALL does not run the rules in $RULES_DIR: the app could not reach the gateway"
    fi
    if data_mounted; then
        ok "data partition mounted"
    else
        stop "the data partition (/var/local) is not mounted"
    fi
}

# The app runs inside iDrive: it was built and checked against this exact version of iDrive's app framework.
check_frameworks() {
    if md5sum -c "$SRC/frameworks.md5" >/dev/null 2>>"$LOG"; then
        ok "iDrive's app framework is the version the app was checked against"
    elif [ "${ENTRYNAV_EARLY_BETA:-0}" = 1 ]; then
        info "Early beta: unverified framework pair on firmware $(firmware); checking loader dependencies"
    else
        stop "iDrive's app framework differs from the version the app was checked against (firmware $(firmware))"
    fi
}

# Check the connector once against the manifests already loaded from this panel.
connector() {
    sed -n 's#^.*<id>\([0-9][0-9]*\)</id>.*$#\1#p' "$1"
}

check_connector() {
    _connector=$(connector "$SRC/$SOURCE/$APP.manifest")
    case $_connector in
        ''|*[!0-9]*) stop "bundled menu connector is missing or ambiguous"; return ;;
    esac
    for _manifest in "$PLUGINS"/*/*.manifest; do
        [ -f "$_manifest" ] || continue
        case $_manifest in "$TARGET"/*) continue ;; esac
        if [ "$(connector "$_manifest")" = "$_connector" ]; then
            stop "connector $_connector is already in use by $_manifest"
        fi
    done
}

check_existing() {
    if ! ours_or_missing "$TARGET"; then
        stop "$TARGET exists and was not installed by EntryNav-Apps: it is left alone"
    fi
    for _d in "$NEW" "$OLD"; do
        if ! ours_or_missing "$_d"; then
            stop "$_d exists and was not made by EntryNav-Apps: it is left alone"
        elif [ -e "$_d" ]; then
            if in_use "$_d"; then
                stop "$_d (an earlier version) is still in use: restart the unit (reboot), then run this again"
            else
                info "$_d, left by an earlier operation, will be deleted"
            fi
        fi
    done
    for _r in "$RULES" "$RULES_NEW"; do
        if ! rules_ours_or_missing "$_r"; then
            stop "$_r exists and was not installed by EntryNav-Apps: it is left alone"
        fi
    done
    if installed; then
        info "installed now: $NAME $(cat "$TARGET/VERSION" 2>/dev/null); it is replaced"
    fi
}

check_space() {
    _need=$(du -sk "$SRC/$SOURCE" 2>/dev/null | { read -r _kb _rest; echo "$_kb"; })
    _free=$(df -k / 2>/dev/null | sed -n '$p' | { read -r _a _b _c _avail _rest; echo "$_avail"; })
    case $_need$_free in
        '' | *[!0-9]*)
            stop "the free space on the system partition could not be read"
            return
            ;;
    esac
    if [ "$_free" -ge $((_need * 2 + MARGIN_KB)) ]; then
        ok "space: $_free KB free, $_need KB needed"
    else
        stop "not enough space on the system partition: $_free KB free, $((_need * 2 + MARGIN_KB)) KB needed"
    fi
}

# The car's loader resolves every library and function the app and its programs need, without running them.
check_libraries() {
    if [ ! -x "$LOADER" ]; then
        stop "$LOADER is missing: not an armhf EntryNav2"
        return
    fi
    _failed=0
    for _f in "$SRC/$SOURCE/lib$APP.so" "$SRC/$SOURCE/$WORKER" "$SRC/$SOURCE/ena-gateway"; do
        _out=$(LD_TRACE_LOADED_OBJECTS=1 LD_WARN=yes LD_BIND_NOW=yes LD_LIBRARY_PATH=$LIBS "$LOADER" "$_f" 2>&1 |
            grep -E 'not found|undefined symbol|not defined in file')
        if [ -n "$_out" ]; then
            _failed=1
            stop "${_f##*/}: something it needs is missing on this car"
            printf '%s\n' "$_out" | while read -r _line; do say "        $_line"; done
        fi
    done
    if [ "$_failed" = 0 ]; then
        ok "every library and function the app uses exists on this car"
    fi
}

do_checks() {
    STOPS=0
    WARNS=0
    say "$NAME check, $(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null), firmware $(firmware)"
    check_basics
    check_archive || return 1
    check_layout
    check_frameworks
    check_connector
    check_existing
    check_space
    if [ "$STOPS" = 0 ]; then
        check_libraries
    fi
    [ "$STOPS" = 0 ]
}

# ---- writing the system partition -------------------------------------------------------------------------------------
begin_write() {
    trap '' HUP INT QUIT TERM PIPE
    trap end_write EXIT
    sync
    if ! mount -o remount,rw / 2>>"$LOG"; then
        say "The system partition could not be made writable: nothing was changed."
        return 1
    fi
    WRITABLE=1
    if root_read_only; then
        say "The system partition is still read-only: nothing was changed."
        return 1
    fi
    return 0
}

end_write() {
    if [ "$WRITABLE" = 1 ]; then
        sync
        if mount -o remount,ro / 2>>"$LOG" && root_read_only; then
            WRITABLE=0
            say "System partition back to read-only."
        else
            say "WARNING: the system partition is still writable. Restart the unit now (reboot): it always starts"
            say "read-only. See $LOG."
        fi
    fi
    trap - EXIT HUP INT QUIT TERM PIPE
}

# Deletes a working folder, or keeps it while a program still uses it (then returns 1).
remove_leftover() {
    if [ ! -e "$1" ] && [ ! -L "$1" ]; then
        return 0
    fi
    if in_use "$1"; then
        say "Kept until a restart (still in use): $1"
        return 1
    fi
    rm -rf "$1" 2>>"$LOG"
    [ ! -e "$1" ] && [ ! -L "$1" ]
}

# The new version in the working folder: marked first (a copy cut short is still recognised), then checked.
stage() {
    mkdir "$NEW" 2>>"$LOG" || return 1
    : >"$NEW/$MARK" || return 1
    cp -R "$SRC/$SOURCE/." "$NEW/" 2>>"$LOG" || return 1
    cp "$SRC/MD5SUMS" "$SRC/frameworks.md5" "$NEW/" 2>>"$LOG" || return 1
    verify_dir "$SRC/MD5SUMS" "$SOURCE" "$NEW" || return 1
    sync
}

# The installed folder aside, the new one in place, the old one back if that fails.
swap_in() {
    if [ -e "$TARGET" ]; then
        mv "$TARGET" "$OLD" 2>>"$LOG" || return 1
    fi
    if ! mv "$NEW" "$TARGET" 2>>"$LOG"; then
        if [ -d "$OLD" ] && [ ! -e "$TARGET" ]; then
            mv "$OLD" "$TARGET" 2>>"$LOG"
        fi
        return 1
    fi
    sync
}

install_rules() {
    rm -f "$RULES_NEW"
    cp "$SRC/etc/firewall/entrynav-$ID.rules" "$RULES_NEW" 2>>"$LOG" || return 1
    [ "$(sed -n 1p "$RULES_NEW")" = "$RULES_MARK" ] || return 1
    mv -f "$RULES_NEW" "$RULES" 2>>"$LOG" || return 1
    sync
}

do_install() {
    if ! do_checks; then
        say "Not installed: $STOPS check(s) failed."
        exit 1
    fi
    begin_write || exit 1
    _failed=
    for _d in $(leftovers); do
        remove_leftover "$_d" || _failed="an earlier version is still in use"
    done
    if [ -z "$_failed" ] && ! stage; then
        _failed="the files could not be copied and checked"
    fi
    if [ -z "$_failed" ] && ! swap_in; then
        _failed="the new version could not be moved in place"
    fi
    if [ -z "$_failed" ] && ! install_rules; then
        _failed="the firewall rule could not be installed"
    fi
    _kept=
    for _d in $(leftovers); do
        remove_leftover "$_d" || _kept=1
    done
    end_write
    if [ -n "$_failed" ]; then
        say "Install failed: $_failed. See $LOG."
        exit 1
    fi
    mkdir -p "$DATA" 2>>"$LOG"
    rm -f "$KEPT_COPY"
    say "$NAME $(cat "$TARGET/VERSION") is installed."
    if [ -n "$_kept" ]; then
        say "The previous version stays until the restart; 'sh $TARGET/setup.sh tidy' deletes it then."
    fi
    add_rules
    # The new rules cover every app, an older one still installed included.
    # shellcheck disable=SC2086 # a rule's words are separate iptables arguments
    iptables_del $RULE_HSFZ_OLD
    if ! gateway_setup ""; then
        say "Without the gateway the app shows no data. Run 'sh $TARGET/setup.sh gateway' with the ignition on."
    fi
    say "Restart iDrive (hold the volume knob for 30 seconds) to load $NAME under My Vehicle."
}

do_uninstall() {
    STOPS=0
    check_basics
    if ! root_read_only; then
        stop "the system partition is writable right now ($(root_field options)): restart the unit first (reboot)"
    fi
    for _d in "$TARGET" "$NEW" "$OLD"; do
        if ! ours_or_missing "$_d"; then
            stop "$_d was not installed by EntryNav-Apps: it is left alone"
        fi
    done
    # The installed folder is renamed to its .old: one left by an update and still in use is in the way.
    if [ -e "$TARGET" ] && [ -e "$OLD" ] && in_use "$OLD"; then
        stop "$OLD (an earlier version) is still in use: restart the unit (reboot), then run this again"
    fi
    for _r in "$RULES" "$RULES_NEW"; do
        if ! rules_ours_or_missing "$_r"; then
            stop "$_r was not installed by EntryNav-Apps: it is left alone"
        fi
    done
    if [ "$STOPS" != 0 ]; then
        say "Nothing was removed."
        exit 1
    fi
    say "Removing $NAME."
    _kept=
    if [ -e "$TARGET" ] || [ -e "$RULES" ] || [ -e "$RULES_NEW" ] || [ -n "$(leftovers)" ]; then
        begin_write || exit 1
        rm -f "$RULES" "$RULES_NEW"
        sync
        if [ -e "$TARGET" ]; then
            remove_leftover "$OLD" && mv "$TARGET" "$OLD" 2>>"$LOG"
        fi
        for _d in $(leftovers); do
            remove_leftover "$_d" || _kept=1
        done
        end_write
    fi
    if [ -e "$TARGET" ] || [ -e "$RULES" ]; then
        say "Not everything could be removed. See $LOG."
        exit 1
    fi
    # The rule stays in force while another of the apps needs it.
    if ! sibling_installed; then
        del_rules
        # shellcheck disable=SC2086 # a rule's words are separate iptables arguments
        iptables_del $RULE_HSFZ_OLD
    fi
    if [ "$PURGE" = 1 ]; then
        for _s in $SETTINGS; do
            rm -f "$DATA/$_s"
        done
        if ! sibling_installed; then
            rm -rf "$DATA"
        fi
        say "Settings deleted."
    fi
    if [ -n "$_kept" ]; then
        # /tmp is emptied by the restart: keep this script on the data partition, where sh can still read it.
        mkdir -p "$DATA" 2>>"$LOG" && cp "$SELF" "$KEPT_COPY" 2>>"$LOG"
        say "$NAME is uninstalled. iDrive keeps using its files until the restart; then run"
        say "'sh $KEPT_COPY tidy' to delete them."
    else
        rm -f "$KEPT_COPY"
        say "$NAME is uninstalled. Restart iDrive to take it off My Vehicle."
    fi
}

do_tidy() {
    check_basics
    if ! root_read_only; then
        say "The system partition is writable right now: restart the unit first (reboot)."
        exit 1
    fi
    _list=$(leftovers)
    if [ -z "$_list" ]; then
        say "Nothing to tidy."
        return 0
    fi
    for _d in $_list; do
        if ! ours_or_missing "$_d"; then
            say "$_d was not made by EntryNav-Apps: it is left alone."
            exit 1
        fi
    done
    begin_write || exit 1
    _pending=
    for _d in $_list; do
        remove_leftover "$_d" || _pending=1
    done
    end_write
    if [ -n "$_pending" ]; then
        say "Still in use: restart the unit (reboot), then run this again."
        exit 1
    fi
    if ! installed; then
        rm -f "$KEPT_COPY"
    fi
    say "Tidied."
}

do_status() {
    say "$NAME status"
    say "firmware: $(firmware)"
    if ! installed; then
        say "installed: no"
        return 0
    fi
    say "installed: $(cat "$TARGET/VERSION" 2>/dev/null)"
    if verify_dir "$TARGET/MD5SUMS" "$SOURCE" "$TARGET"; then
        say "files: intact"
    else
        say "files: damaged"
    fi
    if rules_ours_or_missing "$RULES" && [ -f "$RULES" ]; then
        say "firewall rule: installed"
    else
        say "firewall rule: missing"
    fi
    if md5sum -c "$TARGET/frameworks.md5" >/dev/null 2>>"$LOG"; then
        say "app framework: as checked"
    else
        say "app framework: changed since the installation"
    fi
    _gw=
    [ -r "$GATEWAY_FILE" ] && read -r _gw _rest <"$GATEWAY_FILE"
    say "gateway: ${_gw:-none}"
    for _d in $(leftovers); do
        if in_use "$_d"; then
            say "left over: $_d (in use until the restart)"
        else
            say "left over: $_d (sh $TARGET/setup.sh tidy deletes it)"
        fi
    done
}

# ---- the gateway ------------------------------------------------------------------------------------------------------
iptables_add() {
    iptables -C "$@" 2>/dev/null || iptables -A "$@" 2>>"$LOG"
}

iptables_del() {
    _n=0
    while iptables -C "$@" 2>/dev/null && [ $_n -lt 8 ]; do
        iptables -D "$@" 2>>"$LOG" || break
        _n=$((_n + 1))
    done
}

# The rules file's three rules, in force now: the file only loads at start-up.
add_rules() {
    # shellcheck disable=SC2086 # a rule's words are separate iptables arguments
    iptables_add $RULE_HSFZ
    # shellcheck disable=SC2086 # a rule's words are separate iptables arguments
    iptables_add $RULE_IDENT_OUT
    # shellcheck disable=SC2086 # a rule's words are separate iptables arguments
    iptables_add $RULE_IDENT_IN
}

del_rules() {
    # shellcheck disable=SC2086 # a rule's words are separate iptables arguments
    iptables_del $RULE_HSFZ
    # shellcheck disable=SC2086 # a rule's words are separate iptables arguments
    iptables_del $RULE_IDENT_OUT
    # shellcheck disable=SC2086 # a rule's words are separate iptables arguments
    iptables_del $RULE_IDENT_IN
}

# Whether $1 can be the gateway on vbr0: link-local (169.254.1.0 to 169.254.254.255), or private (10/8, 172.16/12,
# 192.168/16) when a laptop's DHCP server handed it out. The apps (src/diagnostic_worker.h) and Flare Drive agree.
valid_gateway_address() {
    case $1 in
        *[!0-9.]* | *..* | .* | *.) return 1 ;;
    esac
    _old_ifs=$IFS
    IFS=.
    # shellcheck disable=SC2086 # split on the dots
    set -- $1
    IFS=$_old_ifs
    [ $# -eq 4 ] || return 1
    for _octet in "$@"; do
        case $_octet in
            0 | [1-9] | [1-9][0-9] | 1[0-9][0-9] | 2[0-4][0-9] | 25[0-5]) ;;
            *) return 1 ;;
        esac
    done
    case $1.$2 in
        169.254) [ "$3" -ge 1 ] && [ "$3" -le 254 ] ;;
        10.* | 192.168) return 0 ;;
        172.*) [ "$2" -ge 16 ] && [ "$2" -le 31 ] ;;
        *) return 1 ;;
    esac
}

save_gateway() {
    mkdir -p "$DATA" 2>>"$LOG" || return 1
    printf '%s\n' "$1" >"$GATEWAY_FILE.new" 2>>"$LOG" || return 1
    mv -f "$GATEWAY_FILE.new" "$GATEWAY_FILE" 2>>"$LOG" || return 1
    sync
}

# Finds the gateway (or checks the one given) and saves the first that answers. Reads only, on the car's network.
gateway_setup() {
    if ! installed || ! data_mounted; then
        say "$NAME is not installed, or the data partition is not mounted."
        return 1
    fi
    _tool=$TARGET/ena-gateway
    add_rules
    if [ -n "$1" ]; then
        if ! valid_gateway_address "$1"; then
            say "$1 can't be the gateway: it is neither link-local (169.254.x.y) nor a private address."
            return 1
        fi
        _candidates=$1
    else
        _current=
        [ -r "$GATEWAY_FILE" ] && read -r _current _rest <"$GATEWAY_FILE"
        if [ -n "$_current" ] && "$_tool" check --host "$_current" >>"$LOG" 2>&1; then
            say "Gateway $_current still answers."
            return 0
        fi
        _candidates=$("$_tool" discover 2>>"$LOG")
        if [ -z "$_candidates" ]; then
            say "No gateway answered. Is the ignition on?"
            return 1
        fi
    fi
    for _ip in $_candidates; do
        _line=
        if valid_gateway_address "$_ip" && _line=$("$_tool" check --host "$_ip" 2>>"$LOG"); then
            if save_gateway "$_ip"; then
                say "$_line. Saved for the apps."
                return 0
            fi
            say "The gateway answers, but $GATEWAY_FILE could not be written."
            return 1
        fi
        say "${_line:-Gateway $_ip does not answer}"
    done
    return 1
}

# ---- main -------------------------------------------------------------------------------------------------------------
SELF=$0
case $SELF in
    /*) ;;
    *) SELF=$(pwd)/$SELF ;;
esac
SRC=${SELF%/*}

command=$1
[ $# -gt 0 ] && shift
PURGE=
ARG=
case $command in
    uninstall)
        case $1 in
            '') ;;
            --purge) PURGE=1 ;;
            *) usage; exit 2 ;;
        esac
        ;;
    gateway)
        ARG=$1
        [ $# -le 1 ] || { usage; exit 2; }
        ;;
    *)
        if [ $# -gt 0 ]; then
            usage
            exit 2
        fi
        ;;
esac
printf '\n== %s %s %s\n' "$ID" "$command" "$(date '+%Y-%m-%d %H:%M:%S' 2>/dev/null)" >>"$LOG" 2>/dev/null

case $command in
    check)
        if do_checks; then
            say "Every check passed: install can go ahead."
        else
            say "$STOPS check(s) failed: install would stop."
            exit 1
        fi
        ;;
    install) do_install ;;
    status) do_status ;;
    gateway) gateway_setup "$ARG" || exit 1 ;;
    tidy | uninstall)
        # Never run from, or stand in, a folder this deletes: an open deleted file would keep the partition writable.
        # Nor from the copy kept on the data partition: --purge may delete it.
        case $SELF in
            "$TARGET"/* | "$NEW"/* | "$OLD"/* | "$DATA"/*)
                if cp "$SELF" "$SELF_COPY"; then
                    cd /
                    exec /bin/sh "$SELF_COPY" "$command" ${PURGE:+--purge}
                fi
                say "Could not copy this script to /tmp: nothing was changed."
                exit 1
                ;;
        esac
        cd /
        if [ "$command" = tidy ]; then do_tidy; else do_uninstall; fi
        ;;
    *)
        usage
        exit 2
        ;;
esac
