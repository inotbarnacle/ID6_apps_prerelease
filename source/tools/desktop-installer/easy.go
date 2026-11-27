package main

import (
	"errors"
	"fmt"
	"strings"
	"time"
)

type request struct {
	Apps                                []string
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

func installableMenus() []plugin {
	var available []plugin
	for _, app := range apps {
		if app.name != "PowerDeck" || powerDeckAvailable() {
			available = append(available, app)
		}
	}
	return available
}

func selectMenus(names []string, removal bool) ([]plugin, error) {
	available := installableMenus()
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
	var selection []plugin
	var err error
	if in.CarPlay {
		if in.WiFi || in.Uninstall || len(in.Apps) != 0 {
			return fail(errors.New("CarPlay Advanced uses its own operation over wired ENET."))
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
		if _, err := usableIP(in.Head); err != nil {
			return fail(err)
		}
		target = endpoint{Head: in.Head, Gateway: in.Gateway, Local: in.Local}
	}
	settings := vehicleInput{Head: target.Head, Gateway: target.Gateway, Local: target.Local,
		Fingerprint: in.Fingerprint, WiFi: in.WiFi, Version: strings.TrimSpace(in.Version),
		DisplayResolution: in.DisplayResolution}
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
	runner.split = strings.Contains(strings.ToLower(string(kernel)), "hu-jacinto")
	settings.Split = runner.split
	if in.CarPlay {
		if !isNBTPlatform(kernel) {
			return fail(errors.New("CarPlay Advanced requires an NBT Evo head unit with QNX 6.5.0 ARM."))
		}
		runner.commandTimeout = 90 * time.Second
		message, err := performCarPlay(runner, in.CheckOnly)
		if err != nil {
			return fail(err)
		}
		state := "installed"
		if in.CheckOnly {
			state = "checked"
		}
		return outcome{State: state, Message: message}
	}
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
		if err := checkConnectors(runner, selection...); err != nil {
			return fail(err)
		}
		return outcome{State: "checked", Message: fmt.Sprintf("Compatible head unit: %s (%s). Gateway: %s. Selected connectors are available.", result.Version, result.Display, target.Gateway)}
	}
	backup, err := install(runner, target.Gateway, result.screen, false, selection...)
	if err != nil {
		if backup != "" {
			return fail(fmt.Errorf("%w. Head-unit transaction folder %s", err, backup))
		}
		return fail(err)
	}
	return outcome{State: "installed", Message: fmt.Sprintf(
		"Installed and verified %d menus on %s (%s). Restart iDrive while parked. Backup on head unit: %s",
		len(selection), result.Version, result.Display, backup)}
}

func (in *request) normalizeAddresses() {
	in.Head = strings.TrimSpace(in.Head)
	in.Gateway = strings.TrimSpace(in.Gateway)
	if in.Gateway == "" {
		in.Gateway = "169.254.21.3"
	}
}
