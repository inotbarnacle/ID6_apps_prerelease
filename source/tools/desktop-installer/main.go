package main

import (
	"crypto/rand"
	"embed"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"runtime"
	"strings"
	"sync"
	"time"
)

//go:embed index.html
var page embed.FS

func powerDeckAvailable() bool {
	_, err := bundled.ReadFile("payload/PowerDeck/PowerDeck.manifest")
	return err == nil
}

func installerHTML() []byte {
	html, _ := page.ReadFile("index.html")
	power := ""
	if powerDeckAvailable() {
		power = `<label><input type="checkbox" name="apps" value="PowerDeck">PowerDeck (N57)</label>`
	}
	return []byte(strings.ReplaceAll(string(html), "<!-- PowerDeck install -->", power))
}

func main() {
	noBrowser := flag.Bool("no-browser", false, "print the local app URL for development")
	flag.Parse()
	listener, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	var entropy [24]byte
	if _, err = rand.Read(entropy[:]); err != nil {
		listener.Close()
		os.Exit(1)
	}
	prefix := "/" + hex.EncodeToString(entropy[:]) + "/"
	host := listener.Addr().String()
	origin := "http://" + host
	url := origin + prefix
	var operations sync.Mutex
	mux := http.NewServeMux()
	html := installerHTML()
	mux.HandleFunc(prefix, func(w http.ResponseWriter, r *http.Request) {
		if r.Host != host {
			http.Error(w, "Invalid host", 403)
			return
		}
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("Referrer-Policy", "no-referrer")
		w.Header().Set("X-Frame-Options", "DENY")
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("Content-Security-Policy", "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'")
		if r.URL.Path == prefix && r.Method == "GET" {
			w.Header().Set("Content-Type", "text/html; charset=utf-8")
			_, _ = w.Write(html)
			return
		}
		if r.Method != "POST" || r.Header.Get("Origin") != origin ||
			!strings.HasPrefix(r.Header.Get("Content-Type"), "application/json") {
			http.Error(w, "Invalid local app request", 403)
			return
		}
		reply := func(value any) {
			w.Header().Set("Content-Type", "application/json")
			_ = json.NewEncoder(w).Encode(value)
		}
		if !operations.TryLock() {
			reply(outcome{Message: "Another operation is running."})
			return
		}
		defer operations.Unlock()
		switch strings.TrimPrefix(r.URL.Path, prefix) {
		case "install":
			var in request
			r.Body = http.MaxBytesReader(w, r.Body, 2048)
			decoder := json.NewDecoder(r.Body)
			decoder.DisallowUnknownFields()
			if err := decoder.Decode(&in); err != nil || decoder.Decode(new(any)) != io.EOF {
				reply(outcome{Message: "Choose your apps and try again."})
				return
			}
			reply(perform(in))
		case "quit":
			reply(inspection{Result: "Installer closed."})
			time.AfterFunc(300*time.Millisecond, func() { os.Exit(0) })
		default:
			http.NotFound(w, r)
		}
	})
	server := http.Server{Handler: mux, ReadHeaderTimeout: 4 * time.Second, ReadTimeout: 6 * time.Second, WriteTimeout: 4 * time.Minute, IdleTimeout: 35 * time.Second, MaxHeaderBytes: 8192}
	fmt.Println(url)
	if !*noBrowser {
		switch runtime.GOOS {
		case "darwin":
			_ = exec.Command("/usr/bin/open", url).Start()
		case "windows":
			_ = exec.Command("rundll32.exe", "url.dll,FileProtocolHandler", url).Start()
		default:
			_ = exec.Command("xdg-open", url).Start()
		}
	}
	_ = server.Serve(listener)
}
