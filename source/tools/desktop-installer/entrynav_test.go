package main

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

type entryTestRunner struct {
	calls   []string
	fail    string
	archive []byte
}

func (r *entryTestRunner) run(cmd string, input []byte, limit int) ([]byte, error) {
	r.calls = append(r.calls, cmd)
	if strings.HasPrefix(cmd, "umask 077; set -C; cat > ") {
		r.archive = bytes.Clone(input)
	}
	if strings.HasPrefix(cmd, "cat /opt/hmi/lib/") {
		return []byte(cmd), nil
	}
	if r.fail != "" && strings.Contains(cmd, r.fail) {
		return []byte("framework differs"), errors.New("check failed")
	}
	return nil, nil
}

func TestEntryNavCompactDisplayUsesCompactArchive(t *testing.T) {
	r := &entryTestRunner{}
	out, err := entryNav(r, request{CheckOnly: true, DisplayResolution: "800x480"}, entryMenus[:1], []byte("Linux hu-entrynav 4.1 armv7l"))
	if err != nil || !strings.Contains(out.Message, "800 × 480") {
		t.Fatalf("compact check: %v, %s", err, out.Message)
	}
	want, err := bundled.ReadFile("payload/entrynav/800x480/boostdeck-" + entryRelease + "-armhf.tar")
	if err != nil || !bytes.Equal(want, r.archive) {
		t.Fatalf("wrong archive staged: %v", err)
	}
}

func TestEntryNavChecksBeforeInstalling(t *testing.T) {
	selected, err := selectEntryMenus([]string{"BoostDeck", "ShiftDeck"})
	if err != nil {
		t.Fatal(err)
	}
	for _, tc := range []struct {
		fail  string
		check bool
		want  int
	}{
		{"shiftdeck-" + entryRelease + " && ENTRYNAV_EARLY_BETA=1 sh setup.sh check", false, 0},
		{"", true, 0}, {"", false, 2},
	} {
		r := &entryTestRunner{fail: tc.fail}
		_, err := entryNav(r, request{CheckOnly: tc.check}, selected, []byte("Linux hu-entrynav 4.1 armv7l"))
		if (err != nil) != (tc.fail != "") {
			t.Fatalf("entryNav returned %v", err)
		}
		got := strings.Count(strings.Join(r.calls, "\n"), "sh setup.sh install")
		if got != tc.want {
			t.Fatalf("ran %d install commands rather than %d", got, tc.want)
		}
		if tc.want > 0 {
			joined := strings.Join(r.calls, "\n")
			if strings.LastIndex(joined, "setup.sh check") > strings.Index(joined, "setup.sh install") {
				t.Fatal("installed before all checks completed")
			}
		}
		if !strings.HasPrefix(r.calls[len(r.calls)-1], "rm -r /tmp/id6apps-") {
			t.Fatal("missing staging cleanup")
		}
	}
}

func TestEntryNavRejectsWrongPlatformAndUnavailableMenus(t *testing.T) {
	if _, err := selectEntryMenus([]string{"UnlistedMenu"}); err == nil {
		t.Fatal("accepted an unavailable app")
	}
	r := &entryTestRunner{}
	if _, err := entryNav(r, request{}, entryMenus, []byte("QNX hu-omap 6.5.0 armle")); err == nil || len(r.calls) != 0 {
		t.Fatalf("QNX target was not rejected before staging: %v, %v", err, r.calls)
	}
}

func TestStageEntireFullscreenBundle(t *testing.T) {
	menus := append(append([]plugin{}, fullscreenApps...), fullscreenCompanions...)
	for _, display := range []panel{{800, 480}, {960, 480}, {1280, 480}, {1440, 540}, {1920, 720}} {
		tag := fmt.Sprintf("%dx%d", display.width, display.height)
		t.Run(tag, func(t *testing.T) {
			dir := t.TempDir()
			if _, err := install(shellFixture{dir: dir}, "169.254.21.3", display, false, menus...); err != nil {
				t.Fatal(err)
			}
			for _, app := range menus {
				target := filepath.Join(dir, "Plugins", app.name)
				if _, err := os.Stat(filepath.Join(target, app.name+".manifest")); err != nil {
					t.Fatal(err)
				}
				bundledPath := "payload/" + app.name + "/" + app.name + ".xml"
				if display.compact() {
					bundledPath = "payload/layouts/" + tag + "/" + app.name + ".xml"
				}
				want, err := bundled.ReadFile(bundledPath)
				if err != nil {
					t.Fatal(err)
				}
				got, err := os.ReadFile(filepath.Join(target, app.name+".xml"))
				if err != nil || !bytes.Equal(got, want) {
					t.Fatalf("%s installed the wrong layout: %v", app.name, err)
				}
			}
			for name, want := range map[string]string{
				"panel.txt":       tag + "\n",
				"screen-size.txt": fmt.Sprintf("%d %d\n", display.width, display.height),
			} {
				got, err := os.ReadFile(filepath.Join(dir, "Plugins", "ID6LibraryLab", name))
				if err != nil || string(got) != want {
					t.Fatalf("%s = %q, want %q (error %v)", name, got, want, err)
				}
			}
			for _, name := range []string{"HudTest", "ID6AirPlayLab"} {
				if _, err := os.Stat(filepath.Join(dir, "Plugins", name)); !os.IsNotExist(err) {
					t.Fatalf("unexpected removed app %s: %v", name, err)
				}
			}
		})
	}
}
