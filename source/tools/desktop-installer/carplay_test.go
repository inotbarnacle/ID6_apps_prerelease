package main

import (
	"errors"
	"strings"
	"testing"
)

type carPlayFixture struct {
	commandLog []string
	files      map[string][]byte
	denyPlan   bool
}

func (r *carPlayFixture) run(command string, input []byte, _ int) ([]byte, error) {
	r.commandLog = append(r.commandLog, command)
	switch {
	case strings.HasPrefix(command, "umask 077; set -C; cat > "):
		r.files[strings.TrimPrefix(command, "umask 077; set -C; cat > ")] = input
	case strings.HasPrefix(command, "cat "):
		return r.files[strings.TrimPrefix(command, "cat ")], nil
	case strings.HasSuffix(command, " --dry-run"):
		if r.denyPlan {
			return []byte("preflight rejected"), errors.New("preflight exit 1")
		}
		return []byte("upstream plan"), nil
	}
	return nil, nil
}

func TestCarPlayPreviewAndInstall(t *testing.T) {
	const stage = "/fs/sda1/opt/id6apps-carplay-fixture"
	const command = "/bin/sh " + stage + "/carplay-advanced.sh install --mode hud --payload " + stage + "/plugin"
	for _, tc := range []struct {
		name                  string
		preview, deny, writes bool
	}{
		{"preview", true, false, false},
		{"install", false, false, true},
		{"blocked by preflight", false, true, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			f := &carPlayFixture{files: make(map[string][]byte), denyPlan: tc.deny}
			payload := map[string][]byte{carPlayScript: []byte("#!/bin/sh"), "plugin/boot.sh": []byte("#!/bin/ksh")}
			_, err := stageCarPlay(f, stage, payload, tc.preview)
			if (err != nil) != tc.deny {
				t.Fatalf("unexpected error %v", err)
			}
			joined := "\n" + strings.Join(f.commandLog, "\n") + "\n"
			if !strings.Contains(joined, "\n"+command+" --dry-run\n") ||
				strings.Contains(joined, "\n"+command+"\n") != tc.writes ||
				!strings.Contains(joined, "; rmdir "+stage+"/plugin "+stage+"\n") {
				t.Fatalf("unexpected commands:\n%s", joined)
			}
		})
	}
}

func TestCarPlayRejectsUnpinnedPayload(t *testing.T) {
	if _, err := readCarPlayPayload([]byte("different release")); err == nil {
		t.Fatal("accepted a release with a different hash")
	}
}
