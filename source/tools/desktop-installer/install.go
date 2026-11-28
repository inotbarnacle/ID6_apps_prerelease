package main

import (
	"bytes"
	"crypto/rand"
	"crypto/sha256"
	"embed"
	"encoding/hex"
	"encoding/xml"
	"errors"
	"fmt"
	"io/fs"
	"net"
	"regexp"
	"slices"
	"strings"
	"time"

	"golang.org/x/crypto/ssh"
)

//go:embed payload
var bundled embed.FS

const (
	root        = "/var/opt/hmi/data/ID5"
	installLock = root + "/GaugeDeck-install-lock"
	fileOpsName = "gaugedeck-fileops"
)

type vehicleInput struct {
	Head, Gateway, Local, Version, Fingerprint, DisplayResolution string
	WiFi, Split, EntryNav                                         bool
	EntryPassword                                                 string
}

type inspection struct {
	Version, Result, Fingerprint, Display, Touch string
	Ready, CanRecover                            bool
	screen                                       panel
	pair                                         frameworkPair
}

var firmwarePattern = regexp.MustCompile(`(?i)\bNBTevo(?:_|[ \t]+)([a-z][0-9]{5}[a-z])\b`)

func firmwareName(text string) string {
	m := firmwarePattern.FindStringSubmatch(text)
	if len(m) == 2 {
		return "NBTevo_" + strings.ToUpper(m[1])
	}
	return ""
}

func sshConnect(in vehicleInput) (*ssh.Client, net.Conn, string, error) {
	config := &ssh.ClientConfig{
		User: "root",
		Config: ssh.Config{
			KeyExchanges: append(append([]string{}, ssh.SupportedAlgorithms().KeyExchanges...),
				"diffie-hellman-group14-sha1", "diffie-hellman-group1-sha1"),
			Ciphers: append(append([]string{}, ssh.SupportedAlgorithms().Ciphers...), "aes128-cbc", "aes256-cbc", "3des-cbc"),
			MACs:    append(append([]string{}, ssh.SupportedAlgorithms().MACs...), "hmac-sha1", "hmac-sha1-96"),
		},
		HostKeyAlgorithms: append(append([]string{}, ssh.SupportedAlgorithms().HostKeys...), "ssh-rsa", "ssh-dss"),
	}
	if in.EntryNav {
		config.Config = ssh.Config{}
		config.HostKeyAlgorithms = nil
	}
	if in.Fingerprint != "" {
		if in.EntryNav {
			config.Auth = []ssh.AuthMethod{ssh.Password(in.EntryPassword)}
		} else {
			config.Auth = []ssh.AuthMethod{ssh.Password("ts&SK412")}
		}
	}
	var observed string
	config.HostKeyCallback = func(_ string, _ net.Addr, key ssh.PublicKey) error {
		observed = ssh.FingerprintSHA256(key)
		if observed != in.Fingerprint {
			return errors.New("SSH fingerprint has not been confirmed")
		}
		return nil
	}
	c, err := enetDial(in.Local, in.Head, "22", 4*time.Second)
	if err != nil {
		return nil, nil, "", fmt.Errorf("could not reach head unit SSH: %w", err)
	}
	_ = c.SetDeadline(time.Now().Add(8 * time.Second))
	conn, channels, requests, err := ssh.NewClientConn(c, net.JoinHostPort(in.Head, "22"), config)
	if err != nil {
		c.Close()
		return nil, nil, observed, err
	}
	return ssh.NewClient(conn, channels, requests), c, observed, nil
}

type remote struct {
	client         *ssh.Client
	socket         net.Conn
	split          bool
	commandTimeout time.Duration
}

type bounded struct {
	data  bytes.Buffer
	limit int
}

func (b *bounded) Write(p []byte) (int, error) {
	if b.data.Len()+len(p) > b.limit {
		return 0, errors.New("SSH response exceeded expected size")
	}
	return b.data.Write(p)
}

func (r remote) run(cmd string, input []byte, limit int) ([]byte, error) {
	if r.split && cmd != "cat /etc/nbt_version.txt" {
		direct := r
		direct.split = false
		routed := "on -f hu-omap /bin/sh -c '" + strings.ReplaceAll(cmd, "'", "'\\''") + "'"
		return direct.run(routed, input, limit)
	}
	timeout := r.commandTimeout
	if timeout == 0 {
		timeout = 8 * time.Second
	}
	_ = r.socket.SetDeadline(time.Now().Add(timeout))
	s, err := r.client.NewSession()
	if err != nil {
		return nil, err
	}
	defer s.Close()
	out, stderr := bounded{limit: limit}, bounded{limit: 2048}
	s.Stdin = bytes.NewReader(input)
	s.Stdout, s.Stderr = &out, &stderr
	if err = s.Run(cmd); err != nil {
		if r.commandTimeout == 0 {
			out.data.Reset()
		}
		if detail := strings.TrimSpace(stderr.data.String()); detail != "" {
			return out.data.Bytes(), fmt.Errorf("%w: %s", err, detail)
		}
		return out.data.Bytes(), err
	}
	return out.data.Bytes(), nil
}

const appFramework = "libAppFramework.A.so"
const coreFramework = "libCoreFramework.A.so"

type frameworkPair struct{ app, core string }

type remoteRunner interface {
	run(cmd string, input []byte, limit int) ([]byte, error)
}

func isNBTPlatform(info []byte) bool {
	platform := strings.Fields(string(info))
	return len(platform) >= 4 && platform[0] == "QNX" && platform[2] == "6.5.0" && platform[len(platform)-1] == "armle"
}

func inspect(r remoteRunner, in vehicleInput, gatewayCheck func(string, string) (string, error)) inspection {
	result := inspection{Fingerprint: in.Fingerprint}
	detected := ""
	if in.Split {
		versionFile, err := r.run("cat /etc/nbt_version.txt", nil, 1024)
		if err == nil {
			detected = firmwareName(string(versionFile))
		}
	}
	if detected == "" {
		versionFile, err := r.run("cat /opt/sys/etc/nbt_version.txt", nil, 1024)
		if err == nil {
			detected = firmwareName(string(versionFile))
		}
	}
	entered := firmwareName(in.Version)
	if in.Version != "" && (entered == "" || firmwarePattern.FindString(in.Version) != in.Version) {
		result.Result = "Enter the full NBTevo code shown on iDrive."
		return result
	}
	if detected != "" && entered != "" && entered != detected {
		result.Version = detected
		result.Result = "The entered version differs from the version reported over SSH."
		return result
	}
	result.Version = detected
	if result.Version == "" {
		result.Version = entered
	}
	if result.Version == "" {
		result.Version = "NBT Evo (version unavailable)"
	}
	info, err := r.run("uname -a", nil, 1024)
	if err != nil || !isNBTPlatform(info) {
		result.Result = "The head unit must report QNX 6.5.0 ARM."
		return result
	}
	var actual frameworkPair
	for _, framework := range []struct {
		name string
		hash *string
	}{{appFramework, &actual.app}, {coreFramework, &actual.core}} {
		raw, err := r.run("cat /opt/hmi/ID5/lib/"+framework.name, nil, 4<<20)
		if err != nil {
			result.Result = "Could not read " + framework.name + " over SSH. Check the head-unit IP and temporary SSH access."
			return result
		}
		*framework.hash = digest(raw)
	}
	result.pair = actual
	display, err := r.run("LD_LIBRARY_PATH=/opt/sys/lib:/lib:/usr/lib /opt/sys/bin/LayerManagerControl get screen 0", nil, 8192)
	if err == nil {
		result.screen = readPanel(display)
	}
	result.Display = result.screen.String()
	if in.DisplayResolution != "" {
		selected := manualPanel(in.DisplayResolution)
		if !selected.supported() {
			result.Result = "Choose a bundled display resolution."
			return result
		}
		if result.screen != (panel{}) && result.screen != selected {
			result.Result = "The manual resolution differs from the active iDrive resolution. Choose the detected resolution or use Automatic."
			return result
		}
		result.screen = selected
		result.Display = result.screen.String() + " (manual selection)"

	} else if !result.screen.automatic() {
		result.Result = "Choose the known iDrive resolution manually, then check compatibility again."
		return result
	}
	coding, _ := r.run(`for f in /var/opt/conn/carplay/carplay.ini /fs/sda1/opt/conn/carplay/carplay.ini; do if test -r "$f"; then sed -n '/^#Coded is:/p' "$f"; break; fi; done`, nil, 1024)
	result.Touch = "Touch coding unknown. Use the iDrive controller to navigate."
	if touchCoding.Match(coding) {
		result.Touch = "Touch coded for factory CarPlay. Tap behavior in the gauge menus needs a parked check."
	}
	if _, err := usableIP(in.Gateway); err != nil || (in.Gateway == in.Head && !in.WiFi) {
		result.Result = "Enter separate head unit and diagnostic gateway ENET addresses."
		return result
	}
	if !in.WiFi {
		hu, err := gatewayCheck(in.Local, in.Gateway)
		if err != nil {
			result.Result = "Could not reach the diagnostic gateway on port 6801. Check the gateway IP and ENET connection."
			return result
		}
		if hu != "" && hu != in.Head && !in.Split && !isStandardHeadPair(hu, in.Head) {
			result.Result = "The gateway reports a different head unit IP. Check the ENET connection."
			return result
		}
	}
	if _, err := r.run("test -d "+root+" && test ! -L "+root+" && test -w "+root+" && test ! -L "+root+"/Plugins && { test ! -e "+root+"/Plugins || { test -d "+root+"/Plugins && test -w "+root+"/Plugins; }; }", nil, 128); err != nil {
		result.Result = "The expected writable ID5 Plugins folder is unavailable."
		return result
	}
	result.Result = "Frameworks matched. The selected apps are ready to stage."
	if !approvedFrameworkPair(actual) {
		result.Result = "Early beta: this framework pair has no approved runtime profile. Installation is available; menu loading may differ."
	}
	result.Ready = true
	return result
}

func digest(b []byte) string { sum := sha256.Sum256(b); return hex.EncodeToString(sum[:]) }

type plugin struct{ name, worker, connector string }

var apps = []plugin{
	{"BoostDeck", "boostdeck-collector-qnx", "100"},
	{"ShiftDeck", "shift-deck-qnx", "104"},
	{"MhdMaps", "mhd-maps-qnx", "103"},
	{"Bootmod3Suite", "bootmod3-qnx", "111"},
	{"MgFlasher", "mgflasher-qnx", "116"},
	{"EngineCodes", "engine-codes-qnx", "102"},
	{"XDriveControl", "xdrive-control-qnx", "112"},
	{"RoadJet", "road-jet-qnx", "217"},
	{"BEAMer", "beamer-qnx", "218"},
	{"LaunchDeck", "shift-deck-qnx", "120"},
	{"WarmDeck", "warmdeck-collector-qnx", "115"},
	{"HybridDeck", "hybriddeck-collector-qnx", "117"},
}

var fullscreenApps = []plugin{
	{"ID6LibraryLab", "id6-library-fullscreen-qnx", "229"},
	{"ID6FullBoostDeck", "", "231"},
	{"ID6FullShiftDeck", "", "232"},
	{"ID6FullMhdMaps", "", "233"},
	{"ID6FullEngineCodes", "", "234"},
	{"ID6FullRoadJet", "", "235"},
	{"ID6FullBEAMer", "", "236"},
	{"ID6FullLaunchDeck", "", "237"},
	{"ID6FullWarmDeck", "", "238"},
}

var fullscreenCompanions = []plugin{
	{"ID6FullBootmod3Suite", "bootmod3-qnx", "239"},
	{"ID6FullMgFlasher", "mgflasher-qnx", "240"},
	{"ID6FullXDriveControl", "xdrive-control-qnx", "241"},
	{"ID6FullHybridDeck", "hybriddeck-collector-qnx", "242"},
}

var fullscreenReplacedMenus = []plugin{
	{"BoostDeck", "", "100"}, {"ShiftDeck", "", "104"}, {"MhdMaps", "", "103"},
	{"EngineCodes", "", "102"}, {"RoadJet", "", "217"}, {"BEAMer", "", "218"},
	{"LaunchDeck", "", "120"}, {"WarmDeck", "", "115"}, {"ExhaustFlap", "", "120"},
	{"AppUpdates", "", "121"},
	{"HudTest", "", "107"},
	{"ID6AirPlayLab", "", "230"},
}

func replacedMenuBackups(r remoteRunner, selection []plugin, replaceAll bool) ([]string, error) {
	var candidates []plugin
	if replaceAll {
		candidates = append(append(append([]plugin{}, apps...), fullscreenApps...), fullscreenReplacedMenus...)
		candidates = append(candidates, fullscreenCompanions...)
	}
	for _, app := range selection {
		if app.name == "ID6LibraryLab" {
			candidates = append(candidates, fullscreenReplacedMenus...)
			break
		}
	}
	seen := make(map[string]bool)
	for _, app := range selection {
		seen[app.name] = true
	}
	var retired []plugin
	for _, app := range candidates {
		if !seen[app.name] {
			retired = append(retired, app)
			seen[app.name] = true
		}
	}
	return installedMenuBackups(r, retired)
}

func (p plugin) fullscreen() bool {
	return p.name == "ID6LibraryLab" || strings.HasPrefix(p.name, "ID6Full")
}

func checkAppLayouts(screen panel, selection []plugin) error {
	for _, app := range selection {
		if (app.name == "RoadJet" || app.name == "BEAMer") && screen != (panel{1280, 480}) {
			return fmt.Errorf("%s currently requires a 1280 × 480 iDrive display", app.name)
		}
	}
	return nil
}

func (p plugin) usesImages() bool {
	return !p.fullscreen()
}

func (p plugin) executables() []string {
	if p.name == "ID6LibraryLab" {
		return []string{p.worker, "beamer-qnx", "boostdeck-collector-qnx", "engine-codes-qnx", "exhaust-flap-worker-qnx",
			"library-usb-update-qnx", "mhd-maps-qnx", "road-jet-qnx", "shift-deck-qnx", "warmdeck-collector-qnx"}
	}
	if p.worker == "" {
		return nil
	}

	if p.name == "MhdMaps" {
		return []string{p.worker, "exhaust-flap-worker-qnx"}
	}
	return []string{p.worker}
}

type manifest struct {
	Name      string `xml:"name"`
	Connector string `xml:"config>dependencies>pluginAppConnector>id"`
}

var pluginPath = regexp.MustCompile(`^/var/opt/hmi/data/ID5/Plugins/[A-Za-z0-9_.+/-]+\.manifest$`)

func checkConnectors(r remoteRunner, selection ...plugin) error {
	return checkConnectorsReplacing(r, nil, selection...)
}

func checkConnectorsReplacing(r remoteRunner, replaced []string, selection ...plugin) error {
	if len(selection) == 0 {
		selection = apps
	}
	list, err := r.run("test ! -L "+root+"/Plugins && { test ! -d "+root+"/Plugins || find "+root+"/Plugins -name '*.manifest' -type f; }", nil, 32768)
	if err != nil {
		return fmt.Errorf("could not inspect existing plugins: %w", err)
	}
	for _, path := range strings.Fields(string(list)) {
		if !pluginPath.MatchString(path) || strings.Contains(path, "..") {
			return errors.New("unexpected existing plugin path")
		}
		app := strings.TrimSuffix(path[strings.LastIndex(path, "/")+1:], ".manifest")
		if slices.Contains(replaced, app+":") && path == root+"/Plugins/"+app+"/"+app+".manifest" {
			continue
		}
		raw, err := r.run("cat "+path, nil, 65536)
		if err != nil {
			return err
		}
		var m manifest
		if err = xml.Unmarshal(raw, &m); err != nil {
			return fmt.Errorf("could not validate existing manifest %s", path)
		}
		for _, app := range selection {
			if strings.TrimSpace(m.Connector) == app.connector && (m.Name != app.name || path != root+"/Plugins/"+app.name+"/"+app.name+".manifest") {
				return fmt.Errorf("connector %s is already used by %s", app.connector, path)
			}
		}
	}
	return nil
}

type blockedInstall struct {
	report     string
	canRecover bool
}

const emptyDirectoryScript = `empty_directory() {
    test -d "$1" && test ! -L "$1" || return 1
    for member in "$1"/* "$1"/.[!.]* "$1"/..?*; do
        if test -e "$member" || test -L "$member"; then return 1; fi
    done
    return 0
}
`

func (e *blockedInstall) Error() string {
	return "The install lock already exists.\n\n" + e.report
}

func install(r remoteRunner, gateway string, screen panel, recoverLock bool, selection ...plugin) (string, error) {
	return installReplacing(r, gateway, screen, recoverLock, false, selection...)
}

func installReplacing(r remoteRunner, gateway string, screen panel, recoverLock, replaceAll bool, selection ...plugin) (string, error) {
	if len(selection) == 0 {
		selection = apps
	}
	if _, err := usableIP(gateway); err != nil {
		return "", err
	}
	if !screen.supported() {
		return "", errors.New("no supported display layout was selected")
	}
	if err := checkAppLayouts(screen, selection); err != nil {
		return "", err
	}
	lockCheck := "if test -e " + installLock + " || test -L " + installLock + "; then echo locked; fi"
	lock, err := r.run(lockCheck, nil, 512)
	if err != nil {
		return "", fmt.Errorf("install lock check failed: %w", err)
	}
	locked := strings.TrimSpace(string(lock)) == "locked"
	if locked {
		state, report, err := inspectInstallLock(r, "")
		if err != nil {
			return "", fmt.Errorf("could not inspect the existing install lock: %w", err)
		}
		if !recoverLock || state != "staged-only" {
			return "", &blockedInstall{report: report, canRecover: state == "staged-only"}
		}
	} else if len(bytes.TrimSpace(lock)) != 0 {
		return "", errors.New("unexpected response from the install-lock check")
	}
	replaced, err := replacedMenuBackups(r, selection, replaceAll)
	if err != nil {
		return "", err
	}
	if err := checkConnectorsReplacing(r, replaced, selection...); err != nil {
		return "", err
	}
	var nonce [8]byte
	if _, err := rand.Read(nonce[:]); err != nil {
		return "", err
	}
	tx := root + "/GaugeDeck-" + hex.EncodeToString(nonce[:])
	if _, err := r.run("umask 077; mkdir "+tx+" && chmod 755 "+tx, nil, 512); err != nil {
		return "", err
	}
	if err := stageFileOps(r, tx); err != nil {
		return tx, err
	}
	if locked {
		state, report, err := inspectInstallLock(r, tx)
		if err != nil {
			return tx, fmt.Errorf("could not recover the existing install lock: %w", err)
		}
		if state != "cleared" {
			return tx, &blockedInstall{report: report}
		}
	}
	verifyFiles := make(map[string][]byte)
	for _, app := range selection {
		stage := tx + "/new-" + app.name
		live := root + "/Plugins/" + app.name
		path := live + "/" + app.name + ".manifest"
		check := emptyDirectoryScript + "test ! -L " + live + " && { test ! -e " + live +
			" || empty_directory " + live + " || { test ! -L " + path + " && test -f " + path + "; }; }"
		if _, err := r.run(check+" && umask 022 && mkdir "+stage, nil, 512); err != nil {
			return tx, fmt.Errorf("could not prepare %s staging directory: %w", app.name, err)
		}
		files, err := fs.ReadDir(bundled, "payload/"+app.name)
		if err != nil {
			return tx, err
		}
		filesData := map[string][]byte{"gateway-ip.txt": []byte(gateway + "\n")}
		executables := app.executables()
		expected := append([]string{app.name + ".manifest", app.name + ".xml", "lib" + app.name + ".so"}, executables...)
		if app.name == "ID6LibraryLab" {
			expected = append(expected, "update-key.der", "screen-size.txt", "panel.txt")
		}
		if app.usesImages() {
			expected = append(expected, app.name+"_ALL_Images.zip")
		}
		for _, file := range files {
			name := file.Name()
			if !slices.Contains(expected, name) {
				return tx, errors.New("unexpected file in embedded payload")
			}
			data, err := bundled.ReadFile("payload/" + app.name + "/" + name)
			if err != nil {
				return tx, err
			}
			if screen.compact() && (name == app.name+".xml" || name == app.name+"_ALL_Images.zip") {
				data, err = bundled.ReadFile(fmt.Sprintf("payload/layouts/%dx%d/%s", screen.width, screen.height, name))
				if err != nil {
					return tx, fmt.Errorf("missing bundled layout for %s: %w", app.name, err)
				}
			}
			filesData[name] = data
		}
		if app.name == "ID6LibraryLab" {
			filesData["screen-size.txt"] = []byte(fmt.Sprintf("%d %d\n", screen.width, screen.height))
			filesData["panel.txt"] = []byte(fmt.Sprintf("%dx%d\n", screen.width, screen.height))
		}
		for _, required := range expected {
			if len(filesData[required]) == 0 {
				return tx, errors.New("embedded payload is incomplete")
			}
		}
		for name, data := range filesData {
			path := stage + "/" + name
			if _, err := r.run("umask 022; set -C; cat > "+path, data, 512); err != nil {
				return tx, fmt.Errorf("could not upload %s: %w", name, err)
			}
			read, err := r.run("cat "+path, nil, len(data)+1)
			if err != nil || !bytes.Equal(read, data) {
				return tx, fmt.Errorf("upload verification failed for %s", name)
			}
			verifyFiles[live+"/"+name] = data
		}
		for _, name := range executables {
			if _, err := r.run("chmod 755 "+stage+"/"+name, nil, 512); err != nil {
				return tx, fmt.Errorf("could not set execute permission on %s: %w", name, err)
			}
		}
	}
	// Keep the lock and rollback in one QNX shell. Pass the script over stdin
	// so SSH only has to execute a short, fixed command.
	if _, err := r.run("/bin/sh -s", publicationScriptWithBackups(tx, replaced, selection...), 1024); err != nil {
		return tx, fmt.Errorf("publication was interrupted: %w", err)
	}
	for path, expected := range verifyFiles {
		actual, err := r.run("cat "+path, nil, len(expected)+1)
		if err != nil {
			return tx, fmt.Errorf("could not read back published file %s: %w", path, err)
		}
		if !bytes.Equal(actual, expected) {
			return tx, fmt.Errorf("published file differs from staged data: %s", path)
		}
	}
	return tx, nil
}

// Uninstall uses the same locked, reversible rename transaction as installation.
// It needs a writable HMI plugin directory but no diagnostic gateway or display profile.
func uninstall(r remoteRunner, selection []plugin) (string, int, error) {
	info, err := r.run("uname -a", nil, 1024)
	fields := strings.Fields(string(info))
	if err != nil || len(fields) < 4 || fields[0] != "QNX" || fields[1] != "hu-omap" ||
		fields[2] != "6.5.0" || fields[len(fields)-1] != "armle" {
		return "", 0, errors.New("Could not verify the QNX 6.5 ARM HMI node.")
	}
	if _, err := r.run("test -d "+root+" && test ! -L "+root+" && test -w "+root+" && test ! -L "+root+"/Plugins", nil, 128); err != nil {
		return "", 0, errors.New("The writable ID5 Plugins folder is unavailable.")
	}
	if err := checkConnectors(r, selection...); err != nil {
		return "", 0, err
	}
	names, err := installedMenuBackups(r, selection)
	if err != nil {
		return "", 0, err
	}
	if len(names) == 0 {
		return "", 0, nil
	}
	var nonce [8]byte
	if _, err := rand.Read(nonce[:]); err != nil {
		return "", 0, err
	}
	tx := root + "/GaugeDeck-" + hex.EncodeToString(nonce[:])
	if _, err := r.run("umask 077; mkdir "+tx+" && chmod 755 "+tx, nil, 512); err != nil {
		return "", 0, err
	}
	if err := stageFileOps(r, tx); err != nil {
		return tx, 0, err
	}
	moved, err := r.run("/bin/sh -s", menuTransactionScript(tx, names), 1024)
	if err != nil {
		return tx, 0, fmt.Errorf("uninstall was interrupted: %w", err)
	}
	return tx, len(strings.Fields(string(moved))), nil
}

func installedMenuBackups(r remoteRunner, selection []plugin) ([]string, error) {
	var names []string
	for _, app := range selection {
		live := root + "/Plugins/" + app.name
		path := live + "/" + app.name + ".manifest"
		cmd := emptyDirectoryScript + "test ! -L " + live + " || exit 1\n" +
			"if test ! -e " + live + "; then exit 0; fi\n" +
			"if empty_directory " + live + "; then echo empty; else " +
			"test ! -L " + path + " && test -f " + path + " && echo manifest && cat " + path + "; fi"
		raw, err := r.run(cmd, nil, 65536)
		if err != nil {
			return nil, fmt.Errorf("could not verify %s: %w", app.name, err)
		}
		if len(raw) == 0 {
			continue
		}
		if strings.TrimSpace(string(raw)) == "empty" {
			names = append(names, app.name+":")
			continue
		}
		kind, body, _ := strings.Cut(string(raw), "\n")
		var identity manifest
		if err := xml.Unmarshal([]byte(body), &identity); strings.TrimSpace(kind) != "manifest" || err != nil ||
			identity.Name != app.name || strings.TrimSpace(identity.Connector) != app.connector {
			return nil, fmt.Errorf("the manifest for %s does not match this installer", app.name)
		}
		names = append(names, app.name+":")
	}
	return names, nil
}

func stageFileOps(r remoteRunner, tx string) error {
	data, err := bundled.ReadFile("payload/tools/" + fileOpsName)
	if err != nil {
		return err
	}
	path := tx + "/" + fileOpsName
	if _, err := r.run("umask 077; set -C; cat > "+path, data, 512); err != nil {
		return fmt.Errorf("could not upload the file operations helper: %w", err)
	}
	actual, err := r.run("cat "+path, nil, len(data)+1)
	if err != nil {
		return fmt.Errorf("could not read back the file operations helper: %w", err)
	}
	if !bytes.Equal(actual, data) {
		return errors.New("file operations helper differs from the bundled copy")
	}
	if _, err := r.run("chmod 700 "+path+" && "+path+" check", nil, 512); err != nil {
		return fmt.Errorf("the head unit could not run the bundled file operations helper: %w", err)
	}
	return nil
}

func inspectInstallLock(r remoteRunner, recoveryTx string) (string, string, error) {
	var script bytes.Buffer
	fmt.Fprintf(&script, "root=%s\nlock=%s\ncurrent=%s\nops=\"$current/%s\"\napps='", root, installLock, recoveryTx, fileOpsName)
	for _, app := range apps {
		fmt.Fprintf(&script, "%s ", app.name)
	}
	script.WriteString(`'
exists() { test -e "$1" || test -L "$1"; }
lock_empty() {
    for member in "$lock"/* "$lock"/.[!.]* "$lock"/..?*; do
        if exists "$member"; then return 1; fi
    done
    return 0
}
staged_only() {
    test -d "$lock" && test ! -L "$lock" && lock_empty || return 1
    for app in $apps; do
        if exists "$root/Plugins/$app"; then return 1; fi
    done
    complete=0
    for tx in "$root"/GaugeDeck-????????????????; do
        if test "$tx" = "$current" || ! exists "$tx"; then continue; fi
        test -d "$tx" && test ! -L "$tx" || return 1
        has_all_stages=1
        for app in $apps; do
            if exists "$tx/old-$app" || exists "$tx/fresh-$app"; then return 1; fi
            if ! test -d "$tx/new-$app" || test -L "$tx/new-$app"; then has_all_stages=0; fi
        done
        if test "$has_all_stages" = 1; then complete=1; fi
    done
    test "$complete" = 1
}
if staged_only; then
    if test -n "$current"; then
        "$ops" unlock "$lock" || exit 1
        echo cleared
        exit 0
    fi
    echo staged-only
else
    echo review
fi
show_path() {
    if test -L "$1"; then state=symlink
    elif test -d "$1"; then state=directory
    elif test -f "$1"; then state=file
    elif test -e "$1"; then state=other
    else state=missing
    fi
    echo "$2: $state"
}
echo "Install lock: $lock"
show_path "$lock" 'Lock type'
if test -d "$lock" && test ! -L "$lock"; then
    if lock_empty; then echo 'Lock contents: empty'; else echo 'Lock contents: nonempty'; fi
fi
echo
echo 'Live app paths:'
for app in $apps; do
    show_path "$root/Plugins/$app" "$app"
done
for tx in "$root"/GaugeDeck-????????????????; do
    if test ! -d "$tx" || test -L "$tx"; then continue; fi
    echo
    echo "Transaction $tx"
    for app in $apps; do
        for prefix in new old fresh; do
            marker="$prefix-$app"
            show_path "$tx/$marker" "$marker"
        done
    done
done
`)
	out, err := r.run("/bin/sh -s", script.Bytes(), 16384)
	if err != nil {
		return "", "", err
	}
	state, report, _ := strings.Cut(string(out), "\n")
	switch state {
	case "cleared", "staged-only", "review":
		return state, strings.TrimSpace(report), nil
	default:
		return "", "", errors.New("unexpected install recovery response")
	}
}

func publicationScript(tx string, selection ...plugin) []byte {
	return publicationScriptWithBackups(tx, nil, selection...)
}

func publicationScriptWithBackups(tx string, replaced []string, selection ...plugin) []byte {
	if len(selection) == 0 {
		selection = apps
	}
	names := make([]string, 0, len(selection))
	for _, app := range selection {
		worker := app.worker
		if worker == "" {
			worker = "-"
		}
		names = append(names, app.name+":"+worker)
		if app.name == "MhdMaps" && !slices.Contains(replaced, "ExhaustFlap:") {
			names = append(names, "ExhaustFlap:")
		}
	}
	return menuTransactionScript(tx, append(names, replaced...))
}

func menuTransactionScript(tx string, names []string) []byte {
	var script bytes.Buffer
	fmt.Fprintf(&script, "root=%s\ntx=%s\nops=\"$tx/%s\"\napps='%s'\n", root, tx, fileOpsName, strings.Join(names, " "))
	script.WriteString(emptyDirectoryScript)
	script.WriteString(`lock="$root/GaugeDeck-install-lock"
fail() { echo "$1" >&2; exit 1; }
paths() {
    live="$root/Plugins/$app"
    old="$tx/old-$app"
    stage="$tx/new-$app"
    fresh="$tx/fresh-$app"
}
cleanup() {
    if test "$locked" = 0; then return; fi
    rollback_failed=0
    if test "$done_ok" = 0; then
        for entry in $apps; do
	            app=${entry%%:*}
            paths
            if test -d "$old" || test -f "$fresh"; then
                if test ! -e "$stage" && test -d "$live"; then
                    "$ops" move "$live" "$stage" || rollback_failed=1
                fi
                if test -d "$old"; then
                    if test -e "$live" || test -L "$live"; then
                        rollback_failed=1
                    else
                        "$ops" move "$old" "$live" || rollback_failed=1
                    fi
                fi
            fi
        done
    fi
    if test "$rollback_failed" = 1; then
        echo 'Rollback needs inspection. The install lock and backups were kept.' >&2
    else
        if ! "$ops" unlock "$lock"; then
            trap - 0
            fail 'Could not release the install lock.'
        fi
    fi
}
locked=0
done_ok=0
trap cleanup 0
trap 'fail "Publication received a stop signal."' HUP INT TERM
if ! mkdir "$lock"; then
    if test -e "$lock" || test -L "$lock"; then
        fail 'The install lock already exists. Check for an active or interrupted install before retrying.'
    fi
    fail 'Could not create the install lock.'
fi
locked=1
test ! -L "$root/Plugins" || fail 'The Plugins folder is a symbolic link.'
mkdir -p "$root/Plugins" || fail 'Could not create the Plugins folder.'
for entry in $apps; do
    app=${entry%%:*}
    worker=${entry#*:}
    paths
    if test -z "$worker" && ! test -e "$live" && ! test -L "$live"; then
        continue
    fi
    test ! -L "$live" || fail "$app plugin path is a symbolic link."
    if test -e "$live"; then
        if empty_directory "$live"; then
            "$ops" move "$live" "$old" || fail "Could not back up $app."
            empty_directory "$old" || fail "$app changed while it was being backed up."
        else
            test -d "$live" && test ! -L "$live/$app.manifest" && test -f "$live/$app.manifest" ||
                fail "$app plugin path cannot be replaced."
            "$ops" move "$live" "$old" || fail "Could not back up $app."
        fi
    else
        : > "$fresh" || fail "Could not record the new $app install."
    fi
    if test -z "$worker"; then
        test ! -e "$live" && test ! -L "$live" && test -d "$old" ||
            fail "Could not verify backup of $app."
        echo "$app"
        continue
    fi
    "$ops" move "$stage" "$live" || fail "Could not publish $app."
    test -f "$live/$app.manifest" || fail "Published $app is missing its manifest."
    if test "$worker" = "-"; then
        test -f "$live/lib$app.so" || fail "Published $app is missing its plugin library."
    else
        test -x "$live/$worker" || fail "Published $app is missing its executable worker."
    fi
done
done_ok=1
`)
	return script.Bytes()
}
