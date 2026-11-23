package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"net/netip"
	"regexp"
	"sort"
	"strings"
	"sync"
	"syscall"
	"time"
)

type endpoint struct {
	Gateway string
	Head    string
	Local   string
}

func usableIP(text string) (netip.Addr, error) {
	ip, err := netip.ParseAddr(text)
	if err != nil || !ip.Is4() || !(ip.IsLinkLocalUnicast() || ip.IsPrivate()) {
		return netip.Addr{}, errors.New("use a private IPv4 address on the car adapter")
	}
	b := ip.As4()
	if ip.IsLinkLocalUnicast() && (b[2] == 0 || b[2] == 255) {
		return netip.Addr{}, errors.New("use a usable ENET link-local address")
	}
	return ip, nil
}

func discover() ([]endpoint, error) {
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return nil, err
	}
	locals := map[string]bool{}
	for _, addr := range addrs {
		if prefix, e := netip.ParsePrefix(addr.String()); e == nil {
			if ip, e := usableIP(prefix.Addr().String()); e == nil && ip.IsLinkLocalUnicast() {
				locals[ip.String()] = true
			}
		}
	}
	if len(locals) == 0 {
		return nil, errors.New("no 169.254.x.x network adapter found; check the ENET cable")
	}
	var wg sync.WaitGroup
	var lock sync.Mutex
	var results []endpoint
	for local := range locals {
		wg.Go(func() {
			gateways, heads := broadcast(local)
			for _, gateway := range gateways {
				head := headIP(local, gateway)
				if head == "" && len(gateways) == 1 && len(heads) == 1 {
					head = heads[0]
				}
				lock.Lock()
				results = append(results, endpoint{gateway, head, local})
				lock.Unlock()
			}
		})
	}
	wg.Wait()
	sort.Slice(results, func(i, j int) bool {
		if results[i].Gateway == results[j].Gateway {
			return results[i].Local < results[j].Local
		}
		return results[i].Gateway < results[j].Gateway
	})
	if len(results) == 0 {
		return nil, errors.New("no ENET gateway replied; use manual IPs or check your cable, ignition and firewall")
	}
	return results, nil
}

var addressTag = regexp.MustCompile(`DIAGADR(10|62|63)`)

func broadcast(local string) (gateways, heads []string) {
	lc := net.ListenConfig{Control: func(_, _ string, c syscall.RawConn) error {
		var err error
		if e := c.Control(func(fd uintptr) { err = enableBroadcast(fd) }); e != nil {
			return e
		}
		return err
	}}
	conn, err := lc.ListenPacket(context.Background(), "udp4", local+":0")
	if err != nil {
		return
	}
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(1800 * time.Millisecond))
	dest := &net.UDPAddr{IP: net.IPv4(169, 254, 255, 255), Port: 6811}
	if _, err := conn.WriteTo([]byte{0, 0, 0, 0, 0, 0x11}, dest); err != nil {
		return
	}
	seen := map[string]bool{}
	buf := make([]byte, 4096)
	for {
		n, remote, err := conn.ReadFrom(buf)
		if err != nil {
			break
		}
		if n < 6 || buf[4] != 0 || buf[5] != 0x11 {
			continue
		}
		ip, err := usableIP(remote.(*net.UDPAddr).IP.String())
		if err != nil || ip.String() == local {
			continue
		}
		tag := addressTag.FindSubmatch(buf[6:n])
		if len(tag) != 2 || seen[ip.String()] {
			continue
		}
		seen[ip.String()] = true
		if string(tag[1]) == "10" {
			gateways = append(gateways, ip.String())
		} else {
			heads = append(heads, ip.String())
		}
	}
	return
}

func enetDial(local, host, port string, limit time.Duration) (net.Conn, error) {
	if _, err := usableIP(host); err != nil {
		return nil, err
	}
	d := net.Dialer{Timeout: limit}
	if local != "" {
		if _, err := usableIP(local); err != nil {
			return nil, err
		}
		d.LocalAddr = &net.TCPAddr{IP: net.ParseIP(local)}
	}
	c, err := d.Dial("tcp4", net.JoinHostPort(host, port))
	if err != nil {
		return nil, err
	}
	// Restrict the local end of legacy SSH and diagnostics to private adapters.
	if _, err := usableIP(c.LocalAddr().(*net.TCPAddr).IP.String()); err != nil {
		c.Close()
		return nil, err
	}
	return c, nil
}

func readDID(local, gateway string, did uint16) ([]byte, error) {
	conn, err := enetDial(local, gateway, "6801", 2*time.Second)
	if err != nil {
		return nil, err
	}
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(2500 * time.Millisecond))
	hi, lo := byte(did>>8), byte(did)
	if _, err = conn.Write([]byte{0, 0, 0, 5, 0, 1, 0xf4, 0x63, 0x22, hi, lo}); err != nil {
		return nil, err
	}
	var header [6]byte
	for i := 0; i < 32; i++ {
		if _, err = io.ReadFull(conn, header[:]); err != nil {
			return nil, err
		}
		n := binary.BigEndian.Uint32(header[:4])
		if n > 4096 {
			return nil, errors.New("oversized ENET identification response")
		}
		body := make([]byte, n)
		if _, err = io.ReadFull(conn, body); err != nil {
			return nil, err
		}
		switch binary.BigEndian.Uint16(header[4:]) {
		case 0x12:
			if _, err = conn.Write([]byte{0, 0, 0, 2, 0, 0x13, 0, 0xf4}); err != nil {
				return nil, err
			}
		case 1:
			if bytes.HasPrefix(body, []byte{0x63, 0xf4, 0x62, hi, lo}) {
				return body[5:], nil
			}
			if len(body) == 5 && bytes.HasPrefix(body, []byte{0x63, 0xf4, 0x7f, 0x22}) && body[4] != 0x78 {
				return nil, fmt.Errorf("DME gateway did not return head unit identification (NRC %02X)", body[4])
			}
		}
	}
	return nil, errors.New("no matching head unit identification response")
}

func headIP(local, gateway string) string {
	value, err := readDID(local, gateway, 0x172a)
	if err != nil {
		return ""
	}
	result := strings.Trim(string(value), "\x00 \r\n")
	if len(value) == 4 {
		result = net.IP(value).String()
	}
	if ip, e := usableIP(result); e == nil && ip.String() != gateway && ip.String() != local {
		return ip.String()
	}
	return ""
}

func checkGateway(local, gateway string) (string, error) {
	if head := headIP(local, gateway); head != "" {
		return head, nil
	}
	// Some units do not answer the head-unit-IP DID. Still require the
	// diagnostic TCP endpoint that the installed workers will use.
	conn, err := enetDial(local, gateway, "6801", 2*time.Second)
	if err != nil {
		return "", err
	}
	_ = conn.Close()
	return "", nil
}

func isStandardHeadPair(a, b string) bool {
	return (a == "169.254.199.99" && b == "169.254.199.119") ||
		(a == "169.254.199.119" && b == "169.254.199.99")
}

func findHead(wifi bool, gateway string) (endpoint, error) {
	configured := gateway
	if !wifi {
		if found, err := discover(); err == nil {
			for _, candidate := range found {
				if candidate.Head != "" {
					if c, err := enetDial(candidate.Local, candidate.Head, "22", time.Second); err == nil {
						c.Close()
						return candidate, nil
					}
				}
			}
			if len(found) == 1 {
				gateway = found[0].Gateway
			}
		}
		for _, host := range []string{"169.254.199.119", "169.254.199.99"} {
			if c, err := enetDial("", host, "22", time.Second); err == nil {
				local := c.LocalAddr().(*net.TCPAddr).IP.String()
				c.Close()
				return endpoint{Head: host, Gateway: gateway, Local: local}, nil
			}
		}
		return endpoint{}, errors.New("No head unit with SSH enabled answered over wired ENET. Check the cable or enter the head unit IP.")
	}
	addrs, err := net.InterfaceAddrs()
	if err != nil {
		return endpoint{}, err
	}
	var candidates []endpoint
	for _, addr := range addrs {
		prefix, err := netip.ParsePrefix(addr.String())
		if err != nil {
			continue
		}
		local := prefix.Addr()
		if _, err := usableIP(local.String()); err != nil {
			continue
		}
		octets := local.As4()
		for last := 1; last < 255; last++ {
			octets[3] = byte(last)
			host := netip.AddrFrom4(octets)
			if host != local && prefix.Contains(host) {
				candidates = append(candidates, endpoint{Local: local.String(), Head: host.String(), Gateway: configured})
			}
		}
	}
	var found []endpoint
	for start := 0; start < len(candidates); start += 48 {
		batch := candidates[start:min(start+48, len(candidates))]
		var group sync.WaitGroup
		var mutex sync.Mutex
		for _, candidate := range batch {
			group.Go(func() {
				conn, err := enetDial(candidate.Local, candidate.Head, "22", 450*time.Millisecond)
				if err != nil {
					return
				}
				conn.Close()
				mutex.Lock()
				found = append(found, candidate)
				mutex.Unlock()
			})
		}
		group.Wait()
	}
	if len(found) == 1 {
		return found[0], nil
	}
	if len(found) > 1 {
		heads := map[string]bool{}
		for _, peer := range found {
			heads[peer.Head] = true
		}
		ips := make([]string, 0, len(heads))
		for ip := range heads {
			ips = append(ips, ip)
		}
		sort.Strings(ips)
		return endpoint{}, fmt.Errorf("Multiple SSH routes responded. Enter your head unit IP in Connection settings. Found %s.", strings.Join(ips, ", "))
	}
	return endpoint{}, errors.New("No head unit with SSH enabled answered over MHD Wi-Fi. Join the adapter Wi-Fi or enter the head unit IP.")
}
