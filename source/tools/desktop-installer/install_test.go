package main

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

var hostFileOps string

func TestMain(m *testing.M) {
	os.Exit(func() int {
		dir, err := os.MkdirTemp("", "gauge-fileops-test-")
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
			return 1
		}
		defer os.RemoveAll(dir)
		hostFileOps = filepath.Join(dir, fileOpsName)
		cmd := exec.Command("cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-o", hostFileOps, "native/fileops.c")
		if out, err := cmd.CombinedOutput(); err != nil {
			fmt.Fprintf(os.Stderr, "build file operations test helper: %v\n%s", err, out)
			return 1
		}
		return m.Run()
	}())
}

type identifyOnly struct {
	version string
	t       *testing.T
}

func (r identifyOnly) run(cmd string, _ []byte, _ int) ([]byte, error) {
	r.t.Helper()
	if cmd != "cat /opt/sys/etc/nbt_version.txt" {
		r.t.Fatalf("unexpected command on blocked firmware: %s", cmd)
	}
	return []byte(r.version + "\r\n"), nil
}

func TestConflictingFirmwareEntryStopsBeforeFrameworkCheck(t *testing.T) {
	got := inspect(identifyOnly{"NBTevo_N18092I", t}, vehicleInput{Version: "NBTevo_Y24073J"}, checkGateway)
	if got.Ready || !strings.Contains(got.Result, "differs") {
		t.Fatal(got)
	}
}

type shellFixture struct {
	dir                 string
	failMapRename       bool
	failRemoveShiftDeck bool
	corruptStagedRead   bool
}

func (f shellFixture) run(command string, input []byte, _ int) ([]byte, error) {
	if command == "uname -a" {
		return []byte("QNX hu-omap 6.5.0 TI-OMAP5432-uEVM armle"), nil
	}
	// Simulate the head unit with real local shell/filesystem semantics.
	const missingUtilities = "printf() { return 126; }\nls() { return 126; }\nrmdir() { echo 'rmdir: cannot execute - No such file or directory' >&2; return 126; }\nmv() { return 126; }\n"
	cmd := strings.ReplaceAll(command, root, f.dir)
	if strings.HasSuffix(command, "/"+fileOpsName+" check") {
		start := strings.LastIndex(cmd, " && ")
		cmd = cmd[:start] + " && " + hostFileOps + " check"
	}
	if f.corruptStagedRead && strings.HasPrefix(command, "cat ") && strings.HasSuffix(command, "/new-BoostDeck/BoostDeck.xml") {
		return []byte("truncated upload"), nil
	}
	if strings.Contains(command, "find "+root+"/Plugins") {
		cmd += " | sed 's|" + f.dir + "|" + root + "|'"
	}
	if command == "/bin/sh -s" {
		input = bytes.ReplaceAll(input, []byte(root), []byte(f.dir))
		input = bytes.ReplaceAll(input, []byte(`"$ops"`), []byte("test_fileops"))
		guard := ""
		if f.failMapRename {
			guard = `case "$1:$2:$3" in move:*/new-MhdMaps:*/Plugins/MhdMaps) return 1;; esac; `
		}
		if f.failRemoveShiftDeck {
			guard = `case "$1:$2:$3" in move:*/Plugins/ShiftDeck:*/old-ShiftDeck) return 1;; esac; `
		}
		input = append([]byte(missingUtilities+"test_fileops() { "+guard+hostFileOps+" \"$@\"; }\n"), input...)
	}
	process := exec.Command("sh", "-c", missingUtilities+cmd)
	process.Stdin = bytes.NewReader(input)
	out, err := process.Output()
	if failure, ok := err.(*exec.ExitError); ok {
		return out, fmt.Errorf("%w: %s", err, bytes.TrimSpace(failure.Stderr))
	}
	return out, err
}

func TestInstallStagesThreeAndRestoresAfterFailedRename(t *testing.T) {
	for _, failure := range []bool{false, true} {
		t.Run(fmt.Sprint(failure), func(t *testing.T) {
			dir := t.TempDir()
			old := filepath.Join(dir, "Plugins", "BoostDeck")
			if err := os.MkdirAll(old, 0755); err != nil {
				t.Fatal(err)
			}
			manifest := []byte("<plugin><name>BoostDeck</name></plugin>")
			if err := os.WriteFile(filepath.Join(old, "BoostDeck.manifest"), manifest, 0644); err != nil {
				t.Fatal(err)
			}
			tx, err := install(shellFixture{dir: dir, failMapRename: failure}, "169.254.21.3", panel{1440, 540}, false, apps[:3]...)
			if failure {
				if err == nil || !strings.Contains(err.Error(), "Could not publish MhdMaps") {
					t.Fatalf("expected rollback, got %v", err)
				}
				got, e := os.ReadFile(filepath.Join(old, "BoostDeck.manifest"))
				if e != nil || !bytes.Equal(got, manifest) {
					t.Fatal("original plugin was not restored", e)
				}
				for _, name := range []string{"ShiftDeck", "MhdMaps"} {
					if _, e := os.Stat(filepath.Join(dir, "Plugins", name)); !os.IsNotExist(e) {
						t.Fatalf("%s was published", name)
					}
				}
				if _, e := os.Stat(filepath.Join(dir, "GaugeDeck-install-lock")); !os.IsNotExist(e) {
					t.Fatalf("rollback left an install lock: %v", e)
				}
				if _, e := install(shellFixture{dir: dir}, "169.254.21.3", panel{1440, 540}, false, apps[:3]...); e != nil {
					t.Fatalf("retry after rollback: %v", e)
				}
				return
			}
			if err != nil {
				t.Fatal(err)
			}
			backup := filepath.Join(dir, filepath.Base(tx), "old-BoostDeck", "BoostDeck.manifest")
			got, e := os.ReadFile(backup)
			if e != nil || !bytes.Equal(got, manifest) {
				t.Fatal("backup was not preserved", e)
			}
			if _, e := os.Stat(filepath.Join(dir, "Plugins", "EngineCodes")); !os.IsNotExist(e) {
				t.Fatalf("unrelated EngineCodes menu was installed: %v", e)
			}
			for _, app := range apps[:3] {
				live := filepath.Join(dir, "Plugins", app.name)
				got, e := os.ReadFile(filepath.Join(live, "gateway-ip.txt"))
				if e != nil || string(got) != "169.254.21.3\n" {
					t.Fatalf("wrong gateway for %s", app.name)
				}
				xml, e := os.ReadFile(filepath.Join(live, app.name+".xml"))
				base, _ := bundled.ReadFile("payload/" + app.name + "/" + app.name + ".xml")
				if e != nil || len(base) == 0 || !bytes.Equal(xml, base) {
					t.Fatal("expected firmware-scaled base layout", e)
				}
				f, e := os.Stat(filepath.Join(live, app.worker))
				if e != nil || f.Mode().Perm()&0111 == 0 {
					t.Fatalf("%s is not executable", app.worker)
				}
			}
		})
	}
}

func TestStagedReadbackMismatchLeavesLivePluginsAlone(t *testing.T) {
	dir := t.TempDir()
	live := filepath.Join(dir, "Plugins", "BoostDeck")
	if err := os.MkdirAll(live, 0755); err != nil {
		t.Fatal(err)
	}
	manifest := []byte("<plugin><name>BoostDeck</name></plugin>")
	path := filepath.Join(live, "BoostDeck.manifest")
	if err := os.WriteFile(path, manifest, 0644); err != nil {
		t.Fatal(err)
	}
	_, err := install(shellFixture{dir: dir, corruptStagedRead: true}, "169.254.21.3", panel{1280, 480}, false)
	if err == nil || !strings.Contains(err.Error(), "upload verification failed for BoostDeck.xml") {
		t.Fatalf("expected upload verification failure, got %v", err)
	}
	actual, err := os.ReadFile(path)
	if err != nil || !bytes.Equal(actual, manifest) {
		t.Fatal("existing plugin was changed", err)
	}
	if _, err := os.Stat(filepath.Join(dir, "GaugeDeck-install-lock")); !os.IsNotExist(err) {
		t.Fatal("publication started after a failed staged readback", err)
	}
}

func TestPublicationLeavesAnExistingLockAlone(t *testing.T) {
	dir := t.TempDir()
	lock := filepath.Join(dir, "GaugeDeck-install-lock")
	if err := os.Mkdir(lock, 0755); err != nil {
		t.Fatal(err)
	}
	fixture := shellFixture{dir: dir}
	if _, err := fixture.run("/bin/sh -s", publicationScript(root+"/GaugeDeck-test"), 1024); err == nil || !strings.Contains(err.Error(), "lock already exists") {
		t.Fatalf("expected a lock error, got %v", err)
	}
	if _, err := os.Stat(lock); err != nil {
		t.Fatal(err)
	}
}

func TestBlockedInstallReportsPreviousTransactionWithoutChanges(t *testing.T) {
	dir := t.TempDir()
	lock := filepath.Join(dir, "GaugeDeck-install-lock")
	tx := filepath.Join(dir, "GaugeDeck-ae68da9e4a9d7ec9")
	for _, path := range []string{lock, filepath.Join(tx, "old-BoostDeck"), filepath.Join(tx, "new-ShiftDeck"), filepath.Join(dir, "Plugins", "BoostDeck")} {
		if err := os.MkdirAll(path, 0755); err != nil {
			t.Fatal(err)
		}
	}
	marker := filepath.Join(tx, "fresh-MhdMaps")
	if err := os.WriteFile(marker, nil, 0644); err != nil {
		t.Fatal(err)
	}
	newTx, err := install(shellFixture{dir: dir}, "169.254.21.3", panel{1280, 480}, true)
	if newTx != "" || err == nil {
		t.Fatalf("expected a read-only lock report, got %q, %v", newTx, err)
	}
	for _, line := range []string{"Lock type: directory", "Live app paths:", "BoostDeck: directory", "GaugeDeck-ae68da9e4a9d7ec9", "old-BoostDeck: directory", "new-ShiftDeck: directory", "fresh-MhdMaps: file"} {
		if !strings.Contains(err.Error(), line) {
			t.Fatalf("report missing %q: %v", line, err)
		}
	}
	for _, path := range []string{lock, filepath.Join(tx, "old-BoostDeck"), marker} {
		if _, err := os.Stat(path); err != nil {
			t.Fatal(err)
		}
	}
	transactions, err := filepath.Glob(filepath.Join(dir, "GaugeDeck-????????????????"))
	if err != nil || len(transactions) != 1 || transactions[0] != tx {
		t.Fatalf("blocked attempt created another transaction: %v, %v", transactions, err)
	}
}

func TestStagedOnlyLockRecoveryInstallsAllApps(t *testing.T) {
	dir := t.TempDir()
	lock := filepath.Join(dir, "GaugeDeck-install-lock")
	previous := filepath.Join(dir, "GaugeDeck-0123456789abcdef")
	for _, path := range []string{lock, filepath.Join(dir, "Plugins")} {
		if err := os.Mkdir(path, 0755); err != nil {
			t.Fatal(err)
		}
	}
	for _, app := range apps {
		if err := os.MkdirAll(filepath.Join(previous, "new-"+app.name), 0755); err != nil {
			t.Fatal(err)
		}
	}
	partial := filepath.Join(dir, "GaugeDeck-fedcba9876543210", "new-BoostDeck")
	if err := os.MkdirAll(partial, 0755); err != nil {
		t.Fatal(err)
	}
	fixture := shellFixture{dir: dir}
	_, err := install(fixture, "169.254.21.3", panel{1280, 480}, false)
	blocked, ok := err.(*blockedInstall)
	if !ok || !blocked.canRecover {
		t.Fatalf("expected a recoverable staged transaction: %v", err)
	}
	// Recheck on confirmation. A file appearing in the lock rules out recovery.
	guard := filepath.Join(lock, ".owner")
	if err := os.WriteFile(guard, []byte("active"), 0600); err != nil {
		t.Fatal(err)
	}
	_, err = install(fixture, "169.254.21.3", panel{1280, 480}, true)
	if blocked, ok := err.(*blockedInstall); !ok || blocked.canRecover {
		t.Fatalf("expected a nonempty lock to block recovery: %v", err)
	}
	if err := os.Remove(guard); err != nil {
		t.Fatal(err)
	}
	if _, err := install(fixture, "169.254.21.3", panel{1280, 480}, true); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(lock); !os.IsNotExist(err) {
		t.Fatal("install lock remains", err)
	}
	for _, app := range apps {
		if _, err := os.Stat(filepath.Join(previous, "new-"+app.name)); err != nil {
			t.Fatal("previous staging was changed", err)
		}
		if worker, err := os.Stat(filepath.Join(dir, "Plugins", app.name, app.worker)); err != nil || worker.Mode().Perm()&0111 == 0 {
			t.Fatal("new worker is missing or not executable", err)
		}
	}
}

type compatibilityFixture struct {
	identifyOnly
	app, core []byte
	display   string
}

func (r compatibilityFixture) run(cmd string, _ []byte, _ int) ([]byte, error) {
	switch cmd {
	case "cat /opt/sys/etc/nbt_version.txt":
		return []byte(r.version), nil
	case "uname -a":
		return []byte("QNX hu-omap 6.5.0 2015/12/01 TI-OMAP5432-uEVM armle\n"), nil
	case "cat /opt/hmi/ID5/lib/" + appFramework:
		return r.app, nil
	case "cat /opt/hmi/ID5/lib/" + coreFramework:
		return r.core, nil
	case "LD_LIBRARY_PATH=/opt/sys/lib:/lib:/usr/lib /opt/sys/bin/LayerManagerControl get screen 0":
		return []byte(r.display), nil
	}
	if strings.HasPrefix(cmd, "for f in ") {
		return []byte("#Coded is: def,1180_480,touch_screen\n"), nil
	}
	if strings.HasPrefix(cmd, "test -d "+root) {
		return nil, nil
	}
	r.t.Fatalf("unexpected check: %s", cmd)
	return nil, nil
}

func TestInspectionAllowsUnverifiedFrameworksAndChecksActivePanel(t *testing.T) {
	const version = "NBTevo_N18032I"
	fixture := compatibilityFixture{
		identifyOnly: identifyOnly{version, t},
		app:          []byte("app framework"), core: []byte("core framework"),
		display: "- resolution: x=1440, y=540\n",
	}
	saved := profiles[version]
	profiles[version] = frameworkPair{digest(fixture.app), digest(fixture.core)}
	defer func() { profiles[version] = saved }()
	input := vehicleInput{Head: "169.254.21.10", Gateway: "169.254.21.3"}
	reportsHead := func(_, _ string) (string, error) { return input.Head, nil }
	got := inspect(fixture, input, reportsHead)
	if !got.Ready || got.screen != (panel{1440, 540}) || !strings.Contains(got.Touch, "Touch coded") {
		t.Fatal(got)
	}
	mismatch := fixture
	mismatch.core = []byte("other core")
	if got := inspect(mismatch, input, reportsHead); !got.Ready || !strings.Contains(got.Result, "Early beta") {
		t.Fatal(got)
	}
	fixture.display = ""
	if got := inspect(fixture, input, reportsHead); got.Ready || got.Display != "Unknown" {
		t.Fatal(got)
	}
	input.DisplayResolution = "1280x480"
	if got := inspect(fixture, input, reportsHead); !got.Ready || got.screen != (panel{1280, 480}) || !strings.Contains(got.Display, "manual") {
		t.Fatal(got)
	}
	if got := inspect(mismatch, input, reportsHead); got.Ready || !strings.Contains(got.Result, "differs") {
		t.Fatal(got)
	}
	fixture.display = "- resolution: x=1920, y=720\n"
	if got := inspect(fixture, input, reportsHead); got.Ready || !strings.Contains(got.Result, "differs") {
		t.Fatal(got)
	}
	input.DisplayResolution = "1920x720"
	if got := inspect(fixture, input, reportsHead); !got.Ready || got.screen != (panel{1920, 720}) {
		t.Fatal(got)
	}
	input.DisplayResolution = ""
	if got := inspect(fixture, input, reportsHead); !got.Ready || got.Display != "1920 × 720" {
		t.Fatal(got)
	}
	fixture.display = ""
	input.DisplayResolution = "1600x600"
	if got := inspect(fixture, input, reportsHead); got.Ready || !strings.Contains(got.Result, "bundled") {
		t.Fatal(got)
	}
	input.DisplayResolution = "800x720"
	if got := inspect(fixture, input, reportsHead); got.Ready || !strings.Contains(got.Result, "bundled") {
		t.Fatal(got)
	}
	input.DisplayResolution = ""
	fixture.display = "- resolution: x=800, y=480\n"
	if got := inspect(fixture, input, reportsHead); !got.Ready || got.screen != (panel{800, 480}) {
		t.Fatal(got)
	}
	if got := inspect(fixture, input, func(_, _ string) (string, error) { return "", fmt.Errorf("connection refused") }); got.Ready || !strings.Contains(got.Result, "port 6801") {
		t.Fatal(got)
	}
	if got := inspect(fixture, input, func(_, _ string) (string, error) { return "169.254.21.100", nil }); got.Ready || !strings.Contains(got.Result, "different head unit") {
		t.Fatal(got)
	}
}

func TestAppSelectionPublishesOnlyChosenApps(t *testing.T) {
	selection, err := choose([]string{"BoostDeck", "EngineCodes"})
	if err != nil {
		t.Fatal(err)
	}
	script := string(publicationScript(root+"/GaugeDeck-test", selection...))
	if !strings.Contains(script, "BoostDeck:boostdeck-collector-qnx EngineCodes:engine-codes-qnx") || strings.Contains(script, "MhdMaps") {
		t.Fatal("publication does not match selection")
	}
	if _, err := choose([]string{"MhdMaps", "MhdMaps"}); err == nil {
		t.Fatal("accepted duplicate app")
	}
	if _, err := choose([]string{"BadApp"}); err == nil {
		t.Fatal("accepted unknown app")
	}
}

func TestUninstallBacksUpSelectionAndRollsBackAfterFailure(t *testing.T) {
	selection, err := selectMenus([]string{"BoostDeck", "ShiftDeck"}, true)
	if err != nil {
		t.Fatal(err)
	}
	for _, fail := range []bool{false, true} {
		t.Run(fmt.Sprint(fail), func(t *testing.T) {
			dir := t.TempDir()
			for _, app := range []plugin{apps[0], apps[1], {name: "OtherApp", connector: "99"}} {
				folder := filepath.Join(dir, "Plugins", app.name)
				if err := os.MkdirAll(folder, 0755); err != nil {
					t.Fatal(err)
				}
				data := fmt.Sprintf("<plugin><name>%s</name><config><dependencies><pluginAppConnector><id>%s</id></pluginAppConnector></dependencies></config></plugin>", app.name, app.connector)
				if err := os.WriteFile(filepath.Join(folder, app.name+".manifest"), []byte(data), 0644); err != nil {
					t.Fatal(err)
				}
				if err := os.WriteFile(filepath.Join(folder, "gateway-ip.txt"), []byte("169.254.20.4\n"), 0644); err != nil {
					t.Fatal(err)
				}
			}
			backup, count, err := uninstall(shellFixture{dir: dir, failRemoveShiftDeck: fail}, selection)
			if fail && err == nil || !fail && (err != nil || count != 2) {
				t.Fatalf("uninstall returned %q, %d, %v", backup, count, err)
			}
			for _, name := range []string{"BoostDeck", "ShiftDeck", "OtherApp"} {
				folder := filepath.Join(dir, "Plugins", name)
				if !fail && name != "OtherApp" {
					if _, err := os.Stat(folder); !os.IsNotExist(err) {
						t.Fatalf("active folder still present: %s, %v", folder, err)
					}
					folder = filepath.Join(dir, filepath.Base(backup), "old-"+name)
				}
				if got, err := os.ReadFile(filepath.Join(folder, "gateway-ip.txt")); err != nil || string(got) != "169.254.20.4\n" {
					t.Fatalf("settings unavailable for %s: %q, %v", name, got, err)
				}
			}
			if _, err := os.Stat(filepath.Join(dir, filepath.Base(installLock))); !os.IsNotExist(err) {
				t.Fatalf("transaction left its lock: %v", err)
			}
		})
	}
}

func TestFullscreenInstallBacksUpReplacedMenus(t *testing.T) {
	for _, fail := range []bool{false, true} {
		t.Run(fmt.Sprint(fail), func(t *testing.T) {
			dir := t.TempDir()
			for _, app := range []plugin{apps[0], apps[1], {name: "AppUpdates", connector: "121"},
				{name: "HudTest", connector: "107"}, apps[3]} {
				folder := filepath.Join(dir, "Plugins", app.name)
				if err := os.MkdirAll(folder, 0755); err != nil {
					t.Fatal(err)
				}
				data := fmt.Sprintf("<plugin><name>%s</name><config><dependencies><pluginAppConnector><id>%s</id></pluginAppConnector></dependencies></config></plugin>", app.name, app.connector)
				if err := os.WriteFile(filepath.Join(folder, app.name+".manifest"), []byte(data), 0644); err != nil {
					t.Fatal(err)
				}
			}
			backup, err := install(shellFixture{dir: dir, failRemoveShiftDeck: fail},
				"169.254.21.3", panel{1280, 480}, false, fullscreenApps...)
			if (err != nil) != fail {
				t.Fatalf("install returned %q, %v", backup, err)
			}
			for _, name := range []string{"BoostDeck", "ShiftDeck", "AppUpdates", "HudTest"} {
				live := filepath.Join(dir, "Plugins", name)
				_, err := os.Stat(live)
				if fail && err != nil || !fail && !os.IsNotExist(err) {
					t.Fatalf("unexpected live state for %s: %v", live, err)
				}
				if !fail {
					if _, err := os.Stat(filepath.Join(dir, filepath.Base(backup), "old-"+name, name+".manifest")); err != nil {
						t.Fatal(err)
					}
				}
			}
			if _, err := os.Stat(filepath.Join(dir, "Plugins", "ID6FullBoostDeck")); fail && !os.IsNotExist(err) || !fail && err != nil {
				t.Fatalf("unexpected full-screen state: %v", err)
			}
			if _, err := os.Stat(filepath.Join(dir, "Plugins", "Bootmod3Suite")); err != nil {
				t.Fatal(err)
			}
		})
	}
}

func TestInstallLaunchDeck(t *testing.T) {
	selection, err := choose([]string{"LaunchDeck"})
	if err != nil {
		t.Fatal(err)
	}
	dir := t.TempDir()
	_, err = install(shellFixture{dir: dir}, "169.254.21.3", panel{1440, 540}, false, selection...)
	if err != nil {
		t.Fatal(err)
	}
	for _, app := range selection {
		live := filepath.Join(dir, "Plugins", app.name)
		worker, err := os.Stat(filepath.Join(live, app.worker))
		if err != nil || worker.Mode().Perm()&0111 == 0 {
			t.Fatalf("%s worker not executable: %v", app.name, err)
		}
	}
	launch := filepath.Join(dir, "Plugins", "LaunchDeck")
	want, err := bundled.ReadFile("payload/LaunchDeck/LaunchDeck.xml")
	if err != nil {
		t.Fatal(err)
	}
	got, err := os.ReadFile(filepath.Join(launch, "LaunchDeck.xml"))
	if err != nil || !bytes.Equal(got, want) {
		t.Fatalf("incorrect LaunchDeck layout: %v", err)
	}
}

func TestDesktopSelectionReplacesManagedSet(t *testing.T) {
	dir := t.TempDir()
	for _, app := range []plugin{apps[0], fullscreenApps[0], fullscreenApps[1],
		{name: "ExternalApp", connector: "99"}, {name: "HudTest", connector: "107"}} {
		folder := filepath.Join(dir, "Plugins", app.name)
		if err := os.MkdirAll(folder, 0755); err != nil {
			t.Fatal(err)
		}
		data := fmt.Sprintf("<plugin><name>%s</name><config><dependencies><pluginAppConnector><id>%s</id></pluginAppConnector></dependencies></config></plugin>", app.name, app.connector)
		if err := os.WriteFile(filepath.Join(folder, app.name+".manifest"), []byte(data), 0644); err != nil {
			t.Fatal(err)
		}
	}
	backup, err := installReplacing(shellFixture{dir: dir}, "169.254.21.3", panel{1280, 480}, false, true, apps[1])
	if err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"BoostDeck", "ID6LibraryLab", "ID6FullBoostDeck", "HudTest"} {
		if _, err := os.Stat(filepath.Join(dir, "Plugins", name)); !os.IsNotExist(err) {
			t.Fatalf("old menu %s remains active: %v", name, err)
		}
		if _, err := os.Stat(filepath.Join(dir, filepath.Base(backup), "old-"+name, name+".manifest")); err != nil {
			t.Fatal(err)
		}
	}
	for _, name := range []string{"ShiftDeck", "ExternalApp"} {
		if _, err := os.Stat(filepath.Join(dir, "Plugins", name, name+".manifest")); err != nil {
			t.Fatal(err)
		}
	}
}

func TestDesktopRecoversOnlyEmptyManagedFolders(t *testing.T) {
	for _, leftover := range []string{"", ".hidden"} {
		t.Run("leftover="+leftover, func(t *testing.T) {
			dir := t.TempDir()
			for _, name := range []string{"ShiftDeck", "BoostDeck", "ExternalApp"} {
				path := filepath.Join(dir, "Plugins", name)
				if err := os.MkdirAll(path, 0755); err != nil {
					t.Fatal(err)
				}
				if leftover != "" && name != "ExternalApp" {
					if err := os.WriteFile(filepath.Join(path, leftover), []byte("keep"), 0644); err != nil {
						t.Fatal(err)
					}
				}
			}
			backup, err := installReplacing(shellFixture{dir: dir}, "169.254.21.3", panel{1280, 480}, false, true, apps[1])
			if leftover != "" {
				if err == nil {
					t.Fatal("a nonempty manifest-less folder was accepted")
				}
				if _, err := os.Stat(filepath.Join(dir, "Plugins", "BoostDeck", leftover)); err != nil {
					t.Fatal(err)
				}
				return
			}
			if err != nil {
				t.Fatal(err)
			}
			for _, name := range []string{"BoostDeck", "ShiftDeck"} {
				files, err := os.ReadDir(filepath.Join(dir, filepath.Base(backup), "old-"+name))
				if err != nil || len(files) != 0 {
					t.Fatalf("expected an empty backup for %s, got %v, %v", name, files, err)
				}
			}
			for _, path := range []string{"Plugins/ExternalApp", "Plugins/ShiftDeck/ShiftDeck.manifest"} {
				if _, err := os.Stat(filepath.Join(dir, path)); err != nil {
					t.Fatal(err)
				}
			}
		})
	}
}

func TestCompactInstallStagesDashboardArtwork(t *testing.T) {
	for _, size := range []panel{{800, 480}, {960, 480}} {
		t.Run(size.String(), func(t *testing.T) {
			selection, err := choose([]string{"BoostDeck", "WarmDeck", "HybridDeck", "MgFlasher"})
			if err != nil {
				t.Fatal(err)
			}
			dir := t.TempDir()
			_, err = install(shellFixture{dir: dir}, "169.254.21.3", size, false, selection...)
			if err != nil {
				t.Fatal(err)
			}
			sizeDir := fmt.Sprintf("%dx%d", size.width, size.height)
			for _, name := range []string{
				"BoostDeck/BoostDeck.xml", "BoostDeck/BoostDeck_ALL_Images.zip",
				"WarmDeck/WarmDeck.xml", "WarmDeck/WarmDeck_ALL_Images.zip",
				"HybridDeck/HybridDeck.xml", "HybridDeck/HybridDeck_ALL_Images.zip",
				"MgFlasher/MgFlasher.xml", "MgFlasher/MgFlasher_ALL_Images.zip",
			} {
				want, err := bundled.ReadFile("payload/layouts/" + sizeDir + "/" + filepath.Base(name))
				if err != nil {
					t.Fatal(err)
				}
				got, err := os.ReadFile(filepath.Join(dir, "Plugins", name))
				if err != nil || !bytes.Equal(want, got) {
					t.Fatalf("wrong compact file %s: %v", name, err)
				}
			}
		})
	}
}

func TestCaptureOnlyProfileCanInstallAsBeta(t *testing.T) {
	const version = "NBTevo_J16383I"
	if !captureOnly[version] || captureOnly["NBTevo_O18184I"] {
		t.Fatal("unexpected capture-only status")
	}
	fixture := compatibilityFixture{
		identifyOnly: identifyOnly{version, t},
		app:          []byte("app"), core: []byte("core"), display: "- resolution: x=1280, y=480\n",
	}
	saved := profiles[version]
	profiles[version] = frameworkPair{digest(fixture.app), digest(fixture.core)}
	defer func() { profiles[version] = saved }()
	got := inspect(fixture, vehicleInput{Head: "169.254.21.10", Gateway: "169.254.21.3"},
		func(_, _ string) (string, error) { return "169.254.21.10", nil })
	if !got.Ready || !strings.Contains(got.Result, "Early beta") {
		t.Fatalf("unexpected capture-only inspection result: %+v", got)
	}
	if profiles["NBTevo_W20204I"] != profiles["NBTevo_V20153I"] || captureOnly["NBTevo_W20204I"] {
		t.Fatal("W20204I should reuse the verified V20153I framework pair")
	}
}

type splitFirmwareFixture struct {
	compatibilityFixture
	jacintoVersion string
}

func (r splitFirmwareFixture) run(cmd string, input []byte, limit int) ([]byte, error) {
	if cmd == "cat /etc/nbt_version.txt" {
		return []byte(r.jacintoVersion), nil
	}
	return r.compatibilityFixture.run(cmd, input, limit)
}

func TestSplitFirmwarePrefersJacintoScreenVersion(t *testing.T) {
	const version = "NBTevo_Y22292I"
	fixture := splitFirmwareFixture{
		compatibilityFixture: compatibilityFixture{
			identifyOnly: identifyOnly{"NBTevo_Z24000I", t},
			app:          []byte("app"), core: []byte("core"),
			display: "- resolution: x=1280, y=480",
		},
		jacintoVersion: "NBTevo Y22292I\n",
	}
	saved := profiles[version]
	profiles[version] = frameworkPair{digest(fixture.app), digest(fixture.core)}
	defer func() { profiles[version] = saved }()
	in := vehicleInput{Split: true, Head: "169.254.21.10", Gateway: "169.254.21.3"}
	gateway := func(_, _ string) (string, error) { return in.Head, nil }
	for _, entered := range []string{"", "nbtevo y22292i", version} {
		in.Version = entered
		if got := inspect(fixture, in, gateway); !got.Ready || got.Version != version {
			t.Fatalf("%q: %+v", entered, got)
		}
	}
}

func TestInspectUsesApprovedFrameworkPairForFirmwareVariants(t *testing.T) {
	app, core := []byte("approved app"), []byte("approved core")
	profile, only := "NBTevo_V20153I", "NBTevo_J16383I"
	savedProfile, savedOnly := profiles[profile], profiles[only]
	profiles[profile] = frameworkPair{digest(app), digest(core)}
	profiles[only] = frameworkPair{digest(app), digest([]byte("capture-only core"))}
	defer func() {
		profiles[profile], profiles[only] = savedProfile, savedOnly
	}()
	in := vehicleInput{Head: "169.254.21.10", Gateway: "169.254.21.3"}
	gateway := func(_, _ string) (string, error) { return in.Head, nil }
	for _, tc := range []struct {
		name, version string
		app, core     []byte
		verified      bool
	}{
		{"unlisted exact pair", "NBTevo_Z24000I", app, core, true},
		{"listed variant exact pair", "NBTevo_W20204I", app, core, true},
		{"only app matches", "NBTevo_Z24000I", app, []byte("other core"), false},
		{"only core matches", "NBTevo_Z24000I", []byte("other app"), core, false},
		{"capture-only pair", "NBTevo_Z24000I", app, []byte("capture-only core"), false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			fixture := compatibilityFixture{
				identifyOnly: identifyOnly{tc.version, t},
				app:          tc.app, core: tc.core, display: "- resolution: x=1280, y=480",
			}
			got := inspect(fixture, in, gateway)
			if !got.Ready || got.Version != tc.version || strings.Contains(got.Result, "Early beta") == tc.verified {
				t.Fatalf("unexpected inspection: %+v", got)
			}
		})
	}
}

func TestSpeedlockBypassSelection(t *testing.T) {
	selection, err := choose([]string{"SpeedlockBypass"})
	if err != nil {
		t.Fatal(err)
	}
	if len(selection) != 1 || selection[0].name != "SpeedlockBypass" || selection[0].connector != "119" {
		t.Fatalf("unexpected SpeedlockBypass selection: %+v", selection)
	}
	if selection[0].usesImages() {
		t.Fatal("SpeedlockBypass should not require an image archive")
	}
	if len(selection[0].executables()) != 0 {
		t.Fatalf("SpeedlockBypass should not require a worker executable: %v", selection[0].executables())
	}
	script := string(publicationScript("/var/opt/hmi/data/ID5/GaugeDeck-0123456789abcdef", selection...))
	if !strings.Contains(script, "apps='SpeedlockBypass:-'") {
		t.Fatalf("unexpected publication script for SpeedlockBypass:\n%s", script)
	}
	removal, err := selectMenus([]string{"SpeedlockBypass"}, true)
	if err != nil || len(removal) != 1 || removal[0].name != "SpeedlockBypass" {
		t.Fatalf("unexpected SpeedlockBypass uninstall selection: %+v %v", removal, err)
	}
}
