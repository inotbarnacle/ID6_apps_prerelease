package main

import (
	"errors"
	"fmt"
	"strings"
	"time"
)

type request struct {
	Apps                                []string
	EntryPassword                       string
	EntryNav, Fullscreen                bool
	Head, Gateway, Local, Fingerprint   string
	Version, DisplayResolution          string
	WiFi, CheckOnly, Uninstall, CarPlay bool
}

type outcome struct {
	State, Message, Head, Gateway, Local, Fingerprint string
}

func choose(names []string) ([]plugin, error) {
	return selectMenus(names, false)
}

func selectMenus(names []string, removal bool) ([]plugin, error) {
	available := apps
	if removal {
		available = append(append([]plugin(nil), apps...),
			plugin{name: "ExhaustFlap", connector: "120"})
	}
	var selected []plugin
	seen := map[string]bool{}
	for _, name := range names {
		if seen[name] {
			return nil, errors.New("One menu was selected twice.")
		}
		seen[name] = true
		for _, app := range available {
			if app.name == name {
				selected = append(selected, app)
				break
			}
		}
	}
	if len(selected) == 0 || len(selected) != len(names) {
		return nil, errors.New("Choose at least one available menu.")
	}
	if !removal && ((seen["MhdMaps"] && seen["Bootmod3Suite"]) ||
		(seen["MgFlasher"] && (seen["MhdMaps"] || seen["Bootmod3Suite"]))) {
		return nil, errors.New("Choose the tuning suite that matches your car: MHD+, Bootmod3, or MG Flasher.")
	}
	return selected, nil
}

func perform(in request) outcome {
	fail := func(err error) outcome { return outcome{Message: err.Error()} }
	if in.CheckOnly && in.Uninstall {
		return fail(errors.New("Choose one operation at a time."))
	}
	if in.CarPlay {
		return fail(errors.New("This installer provides ID6Apps menus."))
	}
	var selection []plugin
	var entrySelection []entryMenu
	var err error
	if in.EntryNav {
		if in.CarPlay || in.Fullscreen || in.WiFi || strings.TrimSpace(in.Head) == "" || in.EntryPassword == "" ||
			(in.DisplayResolution != "" && in.DisplayResolution != "1280x480" && in.DisplayResolution != "800x480") {
			return fail(errors.New("EntryNav2 needs wired ENET, the head-unit IP, its SSH password, and an 800 × 480 or 1280 × 480 display."))
		}
		entrySelection, err = selectEntryMenus(in.Apps)
		if err != nil {
			return fail(err)
		}
	} else if in.Fullscreen {
		if in.Uninstall && len(in.Apps) != 0 {
			return fail(errors.New("Choose one operation at a time."))
		}
		selection = fullscreenApps
		if in.Uninstall {
			selection = append(append([]plugin{}, fullscreenApps...), fullscreenCompanions...)
		}
		if len(in.Apps) != 0 {
			var companions []plugin
			companions, err = choose(in.Apps)
			if err != nil {
				return fail(err)
			}
			alternateTuning := false
			for _, app := range companions {
				switch app.name {
				case "Bootmod3Suite", "MgFlasher":
					alternateTuning = true
				case "XDriveControl", "HybridDeck":
				default:
					return fail(errors.New("That menu is already available in the full-screen bundle."))
				}
			}
			selection = nil
			for _, app := range fullscreenApps {
				if app.name != "ID6FullMhdMaps" || !alternateTuning {
					selection = append(selection, app)
				}
			}
			for _, app := range companions {
				for _, full := range fullscreenCompanions {
					if full.name == "ID6Full"+app.name {
						selection = append(selection, full)
						break
					}
				}
			}
		}
	} else {
		selection, err = selectMenus(in.Apps, in.Uninstall)
		if err != nil {
			return fail(err)
		}
	}
	in.normalizeAddresses()
	if _, err := usableIP(in.Gateway); err != nil {
		return fail(err)
	}
	var target endpoint
	if in.Head == "" {
		target, err = findHead(in.WiFi, in.Gateway)
		if err != nil {
			return fail(err)
		}
	} else {
		if _, err := peerIP(in.Head); err != nil {
			return fail(err)
		}
		target = endpoint{Head: in.Head, Gateway: in.Gateway, Local: in.Local}
	}
	settings := vehicleInput{Head: target.Head, Gateway: target.Gateway, Local: target.Local,
		Fingerprint: in.Fingerprint, WiFi: in.WiFi, Version: strings.TrimSpace(in.Version),
		DisplayResolution: in.DisplayResolution, EntryNav: in.EntryNav, EntryPassword: in.EntryPassword}
	client, socket, observed, err := sshConnect(settings)
	if err != nil {
		if observed != "" && observed != in.Fingerprint {
			return outcome{State: "trust", Head: target.Head, Gateway: target.Gateway, Local: target.Local, Fingerprint: observed}
		}
		return fail(fmt.Errorf("SSH could not connect to %s. Check that temporary SSH access is enabled. %w", target.Head, err))
	}
	defer client.Close()
	runner := remote{client: client, socket: socket}
	kernel, err := runner.run("uname -a", nil, 1024)
	if err != nil {
		return fail(err)
	}
	if in.EntryNav {
		runner.commandTimeout = 90 * time.Second
		result, err := entryNav(runner, in, entrySelection, kernel)
		if err != nil {
			return fail(err)
		}
		return result
	}
	runner.split = strings.Contains(strings.ToLower(string(kernel)), "hu-jacinto")
	settings.Split = runner.split
	if in.Uninstall {
		backup, count, err := uninstall(runner, selection)
		if err != nil {
			if backup != "" {
				return fail(fmt.Errorf("%w. Head-unit transaction folder %s", err, backup))
			}
			return fail(err)
		}
		if count == 0 {
			return outcome{State: "uninstalled", Message: "None of the selected menus are installed."}
		}
		return outcome{State: "uninstalled", Message: fmt.Sprintf(
			"Removed %d selected menus. Restart iDrive while parked. Backup on head unit: %s", count, backup)}
	}
	result := inspect(runner, settings, checkGateway)
	if !result.Ready {
		return fail(errors.New(result.Result))
	}
	if err := checkAppLayouts(result.screen, selection); err != nil {
		return fail(err)
	}
	if in.CheckOnly {
		replaced, err := replacedMenuBackups(runner, selection, true)
		if err != nil {
			return fail(err)
		}
		if err := checkConnectorsReplacing(runner, replaced, selection...); err != nil {
			return fail(err)
		}
		if in.Fullscreen && result.pair != profiles["NBTevo_N18032I"] {
			result.Result = "Early beta: this firmware has not been validated with the full-screen renderer."
		}
		return outcome{State: "checked", Message: fmt.Sprintf("%s (%s). Gateway: %s. Selected connectors are available.\n%s", result.Version, result.Display, target.Gateway, result.Result)}
	}
	backup, err := installReplacing(runner, target.Gateway, result.screen, false, true, selection...)
	if err != nil {
		if backup != "" {
			return fail(fmt.Errorf("%w. Head-unit transaction folder %s", err, backup))
		}
		return fail(err)
	}
	message := fmt.Sprintf(
		"Installed and verified %d menus on %s (%s). Restart iDrive while parked. Backup on head unit: %s",
		len(selection), result.Version, result.Display, backup)
	if !approvedFrameworkPair(result.pair) || (in.Fullscreen && result.pair != profiles["NBTevo_N18032I"]) {
		message += "\nEarly beta: verify menu loading on this firmware while parked."
	}
	return outcome{State: "installed", Message: message}
}

func (in *request) normalizeAddresses() {
	in.Head = strings.TrimSpace(in.Head)
	in.Gateway = strings.TrimSpace(in.Gateway)
	if in.Gateway == "" {
		in.Gateway = "169.254.21.3"
	}
}
