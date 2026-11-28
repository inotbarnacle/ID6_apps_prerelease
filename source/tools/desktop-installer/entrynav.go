package main

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"regexp"
	"slices"
	"strings"
)

const entryRelease = "0.1.3-test.1"

type entryMenu struct{ name, id, directory string }

var entryMenus = []entryMenu{
	{"BoostDeck", "boostdeck", "BoostDeck"},
	{"ShiftDeck", "shiftdeck", "ShiftDeck"},
	{"EngineCodes", "engine-codes", "EngineCodes"},
	{"MhdMaps", "mhd-maps", "MhdMaps"},
	{"Bootmod3Suite", "bm3-maps", "Bm3Maps"},
	{"WarmDeck", "warmdeck", "WarmDeck"},
	{"LaunchDeck", "launchdeck", "LaunchDeck"},
	{"HybridDeck", "hybriddeck", "HybridDeck"},
	{"RoadJet", "road-jet", "RoadJet"},
	{"BEAMer", "beamer", "BEAMer"},
	{"XDriveControl", "xdrive-control", "XDriveControl"},
	{"MgFlasher", "mgflasher", "MgFlasher"},
}

func selectEntryMenus(names []string) ([]entryMenu, error) {
	var selected []entryMenu
	seen := make(map[string]bool)
	for _, name := range names {
		if seen[name] {
			return nil, errors.New("One EntryNav2 menu was selected twice.")
		}
		seen[name] = true
		for _, app := range entryMenus {
			if app.name == name {
				selected = append(selected, app)
				break
			}
		}
	}
	if len(selected) == 0 || len(selected) != len(names) {
		return nil, errors.New("Choose menus available for EntryNav2.")
	}
	return selected, nil
}

type entryProfile struct {
	Version, AppSHA256, CoreSHA256 string
}

var entryVersionPattern = regexp.MustCompile(`^[0-9]+\.[0-9]+\.[0-9]+(-(test|beta)\.[0-9]+)?$`)

func selectEntryRelease(r remoteRunner) (string, string, error) {
	var profiles []entryProfile
	if packed, err := bundled.ReadFile("payload/entrynav/profiles.json"); err == nil {
		if err := json.Unmarshal(packed, &profiles); err != nil {
			return "", "", err
		}
	}
	app, err := r.run("cat /opt/hmi/lib/libAppFramework.A.so", nil, 8<<20)
	if err != nil || len(app) == 0 {
		return "", "", errors.New("Could not read the EntryNav2 app framework over SSH.")
	}
	core, err := r.run("cat /opt/hmi/lib/libCoreFramework.A.so", nil, 8<<20)
	if err != nil || len(core) == 0 {
		return "", "", errors.New("Could not read the EntryNav2 core framework over SSH.")
	}
	appHash, coreHash := digest(app), digest(core)
	observed := fmt.Sprintf("EntryNav2 framework SHA-256\nApp: %s\nCore: %s", appHash, coreHash)
	if version, err := r.run("cat /etc/version", nil, 1024); err == nil {
		if fields := strings.Fields(string(version)); len(fields) > 0 {
			observed = "EntryNav2 firmware: " + fields[0] + "\n" + observed
		}
	}
	for _, profile := range profiles {
		if profile.AppSHA256 == appHash && profile.CoreSHA256 == coreHash {
			if !entryVersionPattern.MatchString(profile.Version) {
				return "", "", errors.New("Bundled EntryNav2 profile has an invalid version.")
			}
			return profile.Version, observed, nil
		}
	}
	return entryRelease, "Early beta fallback payload. Menu loading and vehicle functions need validation.\n" + observed, nil
}

func entryNav(r remoteRunner, in request, selected []entryMenu, kernel []byte) (outcome, error) {
	if !strings.HasPrefix(string(kernel), "Linux ") || !strings.Contains(string(kernel), "arm") {
		return outcome{}, errors.New("EntryNav2 requires an ARM Linux head unit.")
	}
	if in.Uninstall {
		var completed []string
		for _, app := range selected {
			setup := "/opt/hmi/data/ro/Plugins/" + app.directory + "/setup.sh"
			mark := "/opt/hmi/data/ro/Plugins/" + app.directory + "/.installed-by-entrynav-apps"
			report, err := r.run("test -f "+mark+" && test ! -L "+setup+" && sh "+setup+" uninstall", nil, 8192)
			if err != nil {
				return outcome{}, fmt.Errorf("EntryNav2 removal stopped at %s after [%s]: %w. %s", app.name, strings.Join(completed, ", "), err, strings.TrimSpace(string(report)))
			}
			completed = append(completed, app.name)
		}
		return outcome{State: "uninstalled", Message: "EntryNav2 removed: " + strings.Join(completed, ", ") + ". Restart iDrive while parked."}, nil
	}

	folder := "payload/entrynav/"
	display := "1280 × 480"
	if in.DisplayResolution == "800x480" {
		folder += "800x480/"
		display = "800 × 480"
	}
	data, err := bundled.ReadFile(folder + "SHA256.json")
	if err != nil {
		return outcome{}, errors.New("This build does not include EntryNav2 archives.")
	}
	var hashes map[string]string
	if err := json.Unmarshal(data, &hashes); err != nil {
		return outcome{}, err
	}
	release, observed, err := selectEntryRelease(r)
	if err != nil {
		return outcome{}, err
	}
	for _, app := range selected {
		filename := app.id + "-" + release + "-armhf.tar"
		if hashes[filename] == "" {
			return outcome{}, fmt.Errorf("No verified %s payload is available for this EntryNav2 framework pair.\n%s", app.name, observed)
		}
	}
	var nonce [12]byte
	if _, err := rand.Read(nonce[:]); err != nil {
		return outcome{}, err
	}
	temporary := "/tmp/id6apps-" + hex.EncodeToString(nonce[:])
	if _, err := r.run("umask 077; mkdir "+temporary, nil, 1024); err != nil {
		return outcome{}, err
	}
	defer r.run("rm -r "+temporary, nil, 1024)
	var firmware []string
	for _, app := range selected {
		filename := app.id + "-" + release + "-armhf.tar"
		archive, err := bundled.ReadFile(folder + filename)
		if err != nil || digest(archive) != hashes[filename] {
			return outcome{}, fmt.Errorf("EntryNav2 archive failed verification for %s.", app.name)
		}
		if _, err := r.run("umask 077; set -C; cat > "+temporary+"/"+filename, archive, 1024); err != nil {
			return outcome{}, err
		}
		cmd := "cd " + temporary + " && tar -xf " + filename + " && cd " + app.id + "-" + release + " && ENTRYNAV_EARLY_BETA=1 sh setup.sh check"
		report, err := r.run(cmd, nil, 8192)
		if err != nil {
			return outcome{}, fmt.Errorf("%s failed EntryNav2 compatibility checks: %w. %s\n%s", app.name, err, strings.TrimSpace(string(report)), observed)
		}
		firmware = append(firmware, app.name)
	}
	if in.CheckOnly {
		return outcome{State: "checked", Message: "EntryNav2 checks passed for " + strings.Join(firmware, ", ") + ". Layout: " + display + ".\n\n" + observed}, nil
	}
	backup := "/var/local/data/EntryNavApps/install-" + hex.EncodeToString(nonce[:])
	var previouslyInstalled []entryMenu
	for _, app := range entryMenus {
		target := "/opt/hmi/data/ro/Plugins/" + app.directory
		mark := target + "/.installed-by-entrynav-apps"
		cmd := "if test ! -L " + target + " && test -f " + mark + " && test ! -L " + mark +
			"; then umask 077; mkdir -p " + backup + " && cp -R " + target + " " + backup + "/ && echo backed-up; fi"
		report, err := r.run(cmd, nil, 1024)
		if err != nil {
			return outcome{}, fmt.Errorf("could not back up %s to %s: %w", app.name, backup, err)
		}
		if strings.TrimSpace(string(report)) == "backed-up" {
			previouslyInstalled = append(previouslyInstalled, app)
		}
	}
	var completed []string
	for _, app := range selected {
		cmd := "cd " + temporary + "/" + app.id + "-" + release + " && ENTRYNAV_EARLY_BETA=1 sh setup.sh install"
		report, err := r.run(cmd, nil, 8192)
		if err != nil {
			return outcome{}, fmt.Errorf("EntryNav2 installation stopped at %s after [%s]: %w. %s. Backup location: %s", app.name, strings.Join(completed, ", "), err, strings.TrimSpace(string(report)), backup)
		}
		completed = append(completed, app.name)
	}
	for _, app := range previouslyInstalled {
		if slices.Contains(selected, app) {
			continue
		}
		setup := "/opt/hmi/data/ro/Plugins/" + app.directory + "/setup.sh"
		report, err := r.run("test ! -L "+setup+" && sh "+setup+" uninstall", nil, 8192)
		if err != nil {
			return outcome{}, fmt.Errorf("installed [%s]; could not retire %s: %w. %s. Backup location: %s",
				strings.Join(completed, ", "), app.name, err, strings.TrimSpace(string(report)), backup)
		}
	}
	message := "EntryNav2 early beta installed: " + strings.Join(completed, ", ") + ". Layout: " + display + ". Restart iDrive while parked."
	if len(previouslyInstalled) != 0 {
		message += " Backup on head unit: " + backup
	}
	return outcome{State: "installed", Message: message}, nil
}
