package main

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"
)

// Pattern represents one named byte pattern from patterns.txt.
type Pattern struct {
	Name    string
	Bytes   []int16 // 0-255 for concrete byte, -1 for wildcard (?)
	RawText string  // Original hex string for display
}

// ParsePatterns reads a patterns.txt file and returns all parsed patterns.
// Duplicate names are kept with a numeric suffix (e.g., "oBasicAttackBase_2").
func ParsePatterns(path string) ([]Pattern, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open patterns file: %w", err)
	}
	defer f.Close()

	var patterns []Pattern
	nameCount := make(map[string]int)

	scanner := bufio.NewScanner(f)
	lineNum := 0
	for scanner.Scan() {
		lineNum++
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}

		// Split on first ':'
		colonIdx := strings.IndexByte(line, ':')
		if colonIdx == -1 {
			continue // skip malformed lines
		}

		name := strings.TrimSpace(line[:colonIdx])
		hexPart := strings.TrimSpace(line[colonIdx+1:])

		if name == "" || hexPart == "" {
			continue
		}

		// Parse hex tokens
		tokens := strings.Fields(hexPart)
		bytes := make([]int16, 0, len(tokens))
		for _, tok := range tokens {
			if tok == "?" {
				bytes = append(bytes, -1)
			} else {
				val, err := strconv.ParseUint(tok, 16, 8)
				if err != nil {
					return nil, fmt.Errorf("line %d: invalid hex byte %q in pattern %s: %w", lineNum, tok, name, err)
				}
				bytes = append(bytes, int16(val))
			}
		}

		if len(bytes) == 0 {
			continue
		}

		// Handle duplicate names
		nameCount[name]++
		displayName := name
		if nameCount[name] > 1 {
			displayName = fmt.Sprintf("%s_%d", name, nameCount[name])
		}

		patterns = append(patterns, Pattern{
			Name:    displayName,
			Bytes:   bytes,
			RawText: hexPart,
		})
	}

	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("read patterns: %w", err)
	}

	return patterns, nil
}
