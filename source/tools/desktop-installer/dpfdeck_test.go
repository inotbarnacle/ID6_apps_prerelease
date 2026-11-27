package main

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"testing/fstest"
)

func dpfFixture(t *testing.T) fstest.MapFS {
	t.Helper()
	data := fstest.MapFS{}
	for _, name := range dpfApp.requiredFiles() {
		data["payload/DPFDeck/"+name] = &fstest.MapFile{Data: []byte("host-only fixture: " + name)}
	}
	data["payload/DPFDeck/DPFDeck.manifest"] = &fstest.MapFile{Data: []byte(`<plugin><name>DPFDeck</name><config><dependencies><pluginAppConnector><id>123</id></pluginAppConnector></dependencies></config></plugin>`)}
	data["payload/tools/"+fileOpsName] = &fstest.MapFile{Data: []byte("host-only fileops placeholder")}
	previous := bundled
	bundled = data
	t.Cleanup(func() { bundled = previous })
	return data
}

func TestDPFIsExplicitOptionalAndRequiresPayload(t *testing.T) {
	data := dpfFixture(t)
	for _, app := range apps {
		if app.name == "DPFDeck" {
			t.Fatal("DPFDeck entered legacy defaults")
		}
	}
	selected, err := choose([]string{"DPFDeck"})
	if err != nil || len(selected) != 1 || selected[0] != dpfApp {
		t.Fatal(selected, err)
	}
	delete(data, "payload/DPFDeck/libDPFDeck.so")
	if _, err = choose([]string{"DPFDeck"}); err == nil || !strings.Contains(err.Error(), "not included") {
		t.Fatal(err)
	}
	selected, err = selectMenus([]string{"DPFDeck"}, true)
	if err != nil || len(selected) != 1 || selected[0] != dpfApp {
		t.Fatal(selected, err)
	}
	if _, err = choose([]string{"BoostDeck"}); err != nil {
		t.Fatal(err)
	}
	if _, err = selectMenus([]string{"DPFDeck", "DPFDeck"}, true); err == nil {
		t.Fatal("duplicate selection accepted")
	}
}

func TestDPFPageAvailabilityAndSyntheticMarker(t *testing.T) {
	data := dpfFixture(t)
	html, err := page.ReadFile("index.html")
	if err != nil {
		t.Fatal(err)
	}
	choice := func(raw []byte) string {
		for _, line := range strings.Split(string(raw), "\n") {
			if strings.Contains(line, `name="apps" value="DPFDeck"`) {
				return line
			}
		}
		t.Fatal("missing DPF choice")
		return ""
	}
	if strings.Contains(choice(installerPage(html)), "disabled") {
		t.Fatal("complete payload disabled")
	}
	data["payload/TEST-ONLY-DPF-FIXTURE"] = &fstest.MapFile{Data: []byte("test only")}
	if dpfPayloadAvailable() || !strings.Contains(choice(installerPage(html)), "disabled") {
		t.Fatal("test payload enabled")
	}
	if _, err = choose([]string{"DPFDeck"}); err == nil {
		t.Fatal("test payload selectable")
	}
	delete(data, "payload/TEST-ONLY-DPF-FIXTURE")
	data["payload/DPFDeck/DPFDeck.manifest"] = &fstest.MapFile{Data: []byte(`<plugin><name>DPFDeck</name><config><dependencies><pluginAppConnector><id>100</id></pluginAppConnector></dependencies></config></plugin>`)}
	if dpfPayloadAvailable() {
		t.Fatal("wrong connector enabled")
	}
}

type dpfNoRemote struct{ t *testing.T }

func (r dpfNoRemote) run(command string, _ []byte, _ int) ([]byte, error) {
	r.t.Fatalf("unexpected remote command: %s", command)
	return nil, nil
}

func TestDPFLayoutGateRunsBeforeRemoteMutation(t *testing.T) {
	for _, screen := range []panel{{1280, 480}, {1440, 540}, {1920, 720}} {
		if err := checkAppLayouts(screen, []plugin{dpfApp}); err != nil {
			t.Fatal(err)
		}
	}
	for _, screen := range []panel{{800, 480}, {960, 480}, {1024, 480}, {0, 0}} {
		if _, err := install(dpfNoRemote{t}, "169.254.21.3", screen, false, dpfApp); err == nil {
			t.Fatal("unsupported DPF layout accepted", screen)
		}
	}
	if err := checkAppLayouts(panel{800, 480}, []plugin{apps[0]}); err != nil {
		t.Fatal("legacy layout changed", err)
	}
}

func dpfOldFolder(t *testing.T, dir string) []byte {
	t.Helper()
	folder := filepath.Join(dir, "Plugins", "DPFDeck")
	if err := os.MkdirAll(folder, 0755); err != nil {
		t.Fatal(err)
	}
	manifest := []byte(`<plugin><name>DPFDeck</name><config><dependencies><pluginAppConnector><id>123</id></pluginAppConnector></dependencies></config></plugin>`)
	for name, data := range map[string][]byte{"DPFDeck.manifest": manifest, "DPFDeck.xml": []byte("old layout"), "gateway-ip.txt": []byte("169.254.20.4\n")} {
		if err := os.WriteFile(filepath.Join(folder, name), data, 0644); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.WriteFile(filepath.Join(dir, "DPFDeck.prefs"), []byte{'D', 1, 1, 0}, 0600); err != nil {
		t.Fatal(err)
	}
	return manifest
}

func TestDPFInstallUpdateUninstallPreservePreferences(t *testing.T) {
	data := dpfFixture(t)
	dir := t.TempDir()
	old := dpfOldFolder(t, dir)
	tx, err := install(shellFixture{dir: dir}, "169.254.21.3", panel{1280, 480}, false, dpfApp)
	if err != nil {
		t.Fatal(err)
	}
	for _, name := range dpfApp.requiredFiles() {
		actual, e := os.ReadFile(filepath.Join(dir, "Plugins", "DPFDeck", name))
		if e != nil || !bytes.Equal(actual, data["payload/DPFDeck/"+name].Data) {
			t.Fatal("published mismatch", name, e)
		}
	}
	mode, err := os.Stat(filepath.Join(dir, "Plugins", "DPFDeck", dpfApp.worker))
	if err != nil || mode.Mode().Perm()&0111 == 0 {
		t.Fatal("worker not executable", err)
	}
	backup, e := os.ReadFile(filepath.Join(dir, filepath.Base(tx), "old-DPFDeck", "DPFDeck.manifest"))
	if e != nil || !bytes.Equal(old, backup) {
		t.Fatal("old folder not preserved", e)
	}
	removed, count, err := uninstall(shellFixture{dir: dir}, []plugin{dpfApp})
	if err != nil || count != 1 {
		t.Fatal(removed, count, err)
	}
	if _, err = os.Stat(filepath.Join(dir, "Plugins", "DPFDeck")); !os.IsNotExist(err) {
		t.Fatal("active folder remains", err)
	}
	if _, err = os.Stat(filepath.Join(dir, filepath.Base(removed), "old-DPFDeck", dpfApp.worker)); err != nil {
		t.Fatal("removed folder not backed up", err)
	}
	preferences, err := os.ReadFile(filepath.Join(dir, "DPFDeck.prefs"))
	if err != nil || !bytes.Equal(preferences, []byte{'D', 1, 1, 0}) {
		t.Fatal("preferences changed", preferences, err)
	}
}

func TestDPFConnectorConflictCannotPublish(t *testing.T) {
	dpfFixture(t)
	dir := t.TempDir()
	folder := filepath.Join(dir, "Plugins", "OtherApp")
	if err := os.MkdirAll(folder, 0755); err != nil {
		t.Fatal(err)
	}
	manifest := []byte(`<plugin><name>OtherApp</name><config><dependencies><pluginAppConnector><id>123</id></pluginAppConnector></dependencies></config></plugin>`)
	if err := os.WriteFile(filepath.Join(folder, "OtherApp.manifest"), manifest, 0644); err != nil {
		t.Fatal(err)
	}
	tx, err := install(shellFixture{dir: dir}, "169.254.21.3", panel{1280, 480}, false, dpfApp)
	if err == nil || tx != "" || !strings.Contains(err.Error(), "connector 123") {
		t.Fatal(tx, err)
	}
	if _, err = os.Stat(filepath.Join(dir, "Plugins", "DPFDeck")); !os.IsNotExist(err) {
		t.Fatal("DPF folder created", err)
	}
}

func TestDPFUploadMismatchOrPublicationFailurePreservesOriginal(t *testing.T) {
	for _, publishFailure := range []bool{false, true} {
		t.Run(fmt.Sprint(publishFailure), func(t *testing.T) {
			dpfFixture(t)
			dir := t.TempDir()
			old := dpfOldFolder(t, dir)
			tx, err := install(shellFixture{dir: dir, corruptDPFRead: !publishFailure, failDPFRename: publishFailure}, "169.254.21.3", panel{1280, 480}, false, dpfApp)
			if err == nil || tx == "" {
				t.Fatal("failure not reported", tx, err)
			}
			actual, e := os.ReadFile(filepath.Join(dir, "Plugins", "DPFDeck", "DPFDeck.manifest"))
			if e != nil || !bytes.Equal(actual, old) {
				t.Fatal("original not restored", e)
			}
			xml, e := os.ReadFile(filepath.Join(dir, "Plugins", "DPFDeck", "DPFDeck.xml"))
			if e != nil || string(xml) != "old layout" {
				t.Fatal("old layout changed", e)
			}
			if _, e = os.Stat(filepath.Join(dir, "GaugeDeck-install-lock")); !os.IsNotExist(e) {
				t.Fatal("lock leaked", e)
			}
		})
	}
}

func TestDPFSyntheticFixtureCannotBuildRelease(t *testing.T) {
	dir := t.TempDir()
	if err := os.MkdirAll(filepath.Join(dir, "payload"), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "payload", "TEST-ONLY-DPF-FIXTURE"), []byte("fixture"), 0600); err != nil {
		t.Fatal(err)
	}
	script, err := os.ReadFile("build-release.sh")
	if err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(dir, "build-release.sh")
	if err = os.WriteFile(path, script, 0700); err != nil {
		t.Fatal(err)
	}
	command := exec.Command("sh", path)
	out, err := command.CombinedOutput()
	if err == nil || !strings.Contains(string(out), "test data cannot be included") {
		t.Fatal("fixture release not blocked", err, string(out))
	}
}
