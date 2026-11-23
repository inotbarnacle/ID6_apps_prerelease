package main

import (
	"archive/zip"
	"bytes"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net/http"
	"path"
	"strings"
	"time"
)

const carPlayURL = "https://github.com/st1ng/nbtevo-carplay-advanced/releases/download/v1.3.0/nbtevo-carplay-advanced.zip"
const carPlaySHA256 = "4eeb77bb97f45fcca29f4a18c7e04d6159ebe3934b58aeba602ba0402ab9341c"
const carPlayScript = "carplay-advanced.sh"
const carPlayLive = "/var/opt/hmi/data/rw/Plugins/CarPlayMenu"

var carPlayHUDFiles = []string{
	"librghook.so", "librghook-kombi.so", "librghook-most.so", "librghook-both.so", "libcpprobe.so",
	"libcpreboot.so", "boot.sh", "CarPlayMenu.manifest", "CarPlayMenu-hud.xml",
	"CarPlayMenu_ALL_Texts.zip", "libCarPlayMenu-hud.so",
}

func downloadCarPlay() (map[string][]byte, error) {
	client := http.Client{Timeout: 35 * time.Second}
	response, err := client.Get(carPlayURL)
	if err != nil {
		return nil, err
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("upstream returned %s", response.Status)
	}
	data, err := io.ReadAll(io.LimitReader(response.Body, 2<<20))
	if err != nil {
		return nil, err
	}
	return readCarPlayPayload(data)
}

func readCarPlayPayload(data []byte) (map[string][]byte, error) {
	if digest(data) != carPlaySHA256 {
		return nil, errors.New("the upstream release checksum did not match")
	}
	archive, err := zip.NewReader(bytes.NewReader(data), int64(len(data)))
	if err != nil {
		return nil, err
	}
	entries := map[string]string{"nbtevo-carplay-advanced/" + carPlayScript: carPlayScript}
	for _, name := range carPlayHUDFiles {
		relative := "plugin/" + name
		entries["nbtevo-carplay-advanced/"+relative] = relative
	}
	files := make(map[string][]byte, len(entries))
	for _, entry := range archive.File {
		relative, wanted := entries[entry.Name]
		if !wanted {
			continue
		}
		if _, duplicate := files[relative]; duplicate || !entry.Mode().IsRegular() ||
			entry.UncompressedSize64 == 0 || entry.UncompressedSize64 > 1<<20 {
			return nil, fmt.Errorf("invalid release file %s", relative)
		}
		reader, err := entry.Open()
		if err != nil {
			return nil, err
		}
		content, err := io.ReadAll(io.LimitReader(reader, (1<<20)+1))
		reader.Close()
		if err != nil || uint64(len(content)) != entry.UncompressedSize64 {
			return nil, fmt.Errorf("could not verify release file %s", relative)
		}
		files[relative] = content
	}
	if len(files) != len(entries) {
		return nil, errors.New("the upstream release is incomplete")
	}
	return files, nil
}

func performCarPlay(r remoteRunner, preview bool) (string, error) {
	if _, err := r.run("test ! -L "+carPlayLive+" && { test ! -e "+carPlayLive+
		" || test -f "+carPlayLive+"/hud_only; }", nil, 512); err != nil {
		return "", errors.New("An existing CarPlay Advanced installation is not in HUD-only mode. Use st1ng’s upstream installer to manage it.")
	}
	files, err := downloadCarPlay()
	if err != nil {
		return "", fmt.Errorf("could not obtain st1ng’s CarPlay Advanced v1.3.0 release: %w", err)
	}
	var nonce [8]byte
	if _, err := rand.Read(nonce[:]); err != nil {
		return "", err
	}
	stage := "/fs/sda1/opt/id6apps-carplay-" + hex.EncodeToString(nonce[:])
	return stageCarPlay(r, stage, files, preview)
}

func stageCarPlay(r remoteRunner, stage string, files map[string][]byte, preview bool) (string, error) {
	if _, err := r.run("umask 077; mkdir "+stage, nil, 512); err != nil {
		return "", fmt.Errorf("could not create the CarPlay staging folder: %w", err)
	}
	cleanup := []string{"rm", "-f"}
	for name := range files {
		cleanup = append(cleanup, path.Join(stage, name))
	}
	defer func() {
		_, _ = r.run(strings.Join(cleanup, " ")+"; rmdir "+stage+"/plugin "+stage, nil, 512)
	}()
	if _, err := r.run("umask 077; mkdir "+stage+"/plugin", nil, 512); err != nil {
		return "", err
	}
	for name, content := range files {
		remotePath := path.Join(stage, name)
		if _, err := r.run("umask 077; set -C; cat > "+remotePath, content, 512); err != nil {
			return "", fmt.Errorf("could not stage %s: %w", name, err)
		}
		actual, err := r.run("cat "+remotePath, nil, len(content)+1)
		if err != nil || !bytes.Equal(content, actual) {
			return "", fmt.Errorf("CarPlay upload verification failed for %s", name)
		}
	}
	command := "/bin/sh " + stage + "/" + carPlayScript + " install --mode hud --payload " + stage + "/plugin"
	plan, err := r.run(command+" --dry-run", nil, 32<<10)
	if err != nil {
		return "", fmt.Errorf("CarPlay Advanced preflight failed: %w\n%s", err, strings.TrimSpace(string(plan)))
	}
	if preview {
		return string(plan) + "\nPreview complete. Temporary upload files are removed.", nil
	}
	output, err := r.run(command, nil, 32<<10)
	if err != nil {
		return "", fmt.Errorf("CarPlay Advanced reported an installation error. Check the on-unit backups under /var/opt/carplay-advanced-backups before retrying: %w\n%s", err, strings.TrimSpace(string(output)))
	}
	if _, err := r.run("test -f "+carPlayLive+"/hud_only && test -f "+carPlayLive+"/librghook.so", nil, 512); err != nil {
		return "", errors.New("CarPlay Advanced did not verify after installation. Inspect the on-unit backup before retrying.")
	}
	return string(output) + "\nRestart iDrive while parked to load the CarPlay Advanced Settings menu.", nil
}
