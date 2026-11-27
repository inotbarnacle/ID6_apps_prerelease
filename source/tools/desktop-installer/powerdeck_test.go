package main

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestPowerDeckSelectionAndLayouts(t *testing.T) {
	selection, err := choose([]string{"PowerDeck"})
	if powerDeckAvailable() {
		if err != nil || len(selection) != 1 || selection[0].connector != "121" {
			t.Fatal(selection, err)
		}
	} else if err == nil {
		t.Fatal("PowerDeck without a bundled payload must not be installable")
	}
	selection, err = selectMenus([]string{"PowerDeck"}, true)
	if err != nil || len(selection) != 1 {
		t.Fatal("PowerDeck removal must remain available", err)
	}
	html := string(installerHTML())
	if strings.Contains(html, `name="apps" value="PowerDeck"`) != powerDeckAvailable() {
		t.Fatal("PowerDeck install checkbox must match its bundled payload")
	}
	if !strings.Contains(html, `name="remove" value="PowerDeck"`) {
		t.Fatal("PowerDeck removal checkbox is missing")
	}
	for _, screen := range []panel{{800, 480}, {960, 480}} {
		if checkAppLayouts(screen, selection) == nil {
			t.Fatalf("PowerDeck runtime graph geometry cannot use compact XML scaling: %v", screen)
		}
		boost, _ := choose([]string{"BoostDeck"})
		if err := checkAppLayouts(screen, boost); err != nil {
			t.Fatal(err)
		}
	}
	for _, screen := range []panel{{1280, 480}, {1440, 540}, {1920, 720}} {
		if err := checkAppLayouts(screen, selection); err != nil {
			t.Fatal(err)
		}
	}
}

func TestPowerDeckInstallPreservesSavedMeasurement(t *testing.T) {
	if !powerDeckAvailable() {
		t.Skip("PowerDeck is included only when staged from its package or an N57 candidate")
	}
	dir := t.TempDir()
	csv := []byte("elapsed_ms,rpm,torque_nm,power_kw\n0,3000,500,157.079633\n")
	if err := os.WriteFile(filepath.Join(dir, "PowerDeck.last.csv"), csv, 0600); err != nil {
		t.Fatal(err)
	}
	selection, _ := choose([]string{"PowerDeck"})
	if _, err := install(shellFixture{dir: dir}, "169.254.21.3", panel{1280, 480}, false, selection...); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"PowerDeck.manifest", "PowerDeck.xml", "PowerDeck_ALL_Images.zip", "libPowerDeck.so", "boostdeck-collector-qnx"} {
		want, err := bundled.ReadFile("payload/PowerDeck/" + name)
		if err != nil {
			t.Fatal(err)
		}
		got, err := os.ReadFile(filepath.Join(dir, "Plugins", "PowerDeck", name))
		if err != nil || !bytes.Equal(got, want) {
			t.Fatalf("PowerDeck payload differs: %s, %v", name, err)
		}
	}
	got, err := os.ReadFile(filepath.Join(dir, "PowerDeck.last.csv"))
	if err != nil || !bytes.Equal(got, csv) {
		t.Fatal("Saved PowerDeck measurement changed", err)
	}
}
