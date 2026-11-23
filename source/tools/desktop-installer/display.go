package main

import (
	"fmt"
	"regexp"
	"strconv"
	"strings"
)

type panel struct{ width, height int }

var (
	resolution  = regexp.MustCompile(`(?im)^[^\r\n]*resolution:\s*x\s*=\s*([0-9]{2,4}),\s*y\s*=\s*([0-9]{2,4})\b`)
	touchCoding = regexp.MustCompile(`(?im)^#Coded is:[^\r\n]*\btouch_screen\b`)
)

func readPanel(output []byte) panel {
	match := resolution.FindSubmatch(output)
	if match == nil {
		return panel{}
	}
	width, _ := strconv.Atoi(string(match[1]))
	height, _ := strconv.Atoi(string(match[2]))
	if width == 0 || height == 0 {
		return panel{}
	}
	return panel{width, height}
}

func (p panel) String() string {
	if p.width == 0 || p.height == 0 {
		return "Unknown"
	}
	return fmt.Sprintf("%d × %d", p.width, p.height)
}

func (p panel) compact() bool {
	return p == (panel{800, 480}) || p == (panel{960, 480})
}

func (p panel) automatic() bool {
	return p.supported()
}

func (p panel) supported() bool {
	switch p {
	case panel{800, 480}, panel{960, 480}, panel{1280, 480}, panel{1440, 540}, panel{1920, 720}:
		return true
	default:
		return false
	}
}

func manualPanel(value string) panel {
	w, h, _ := strings.Cut(value, "x")
	width, widthErr := strconv.Atoi(w)
	height, heightErr := strconv.Atoi(h)
	if widthErr != nil || heightErr != nil {
		return panel{}
	}
	return panel{width, height}
}
