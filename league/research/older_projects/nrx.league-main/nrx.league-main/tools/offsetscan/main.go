package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
)

func main() {
	dumpPath := flag.String("dump", "", "Path to PE dump file (required)")
	patternsPath := flag.String("patterns", "", "Path to patterns.txt (default: auto-detect)")
	format := flag.String("format", "console", "Output format: console, header, json")
	verbose := flag.Bool("verbose", false, "Show extra detail (matched bytes, multiple matches)")
	flag.Parse()

	if *dumpPath == "" {
		fmt.Fprintln(os.Stderr, "Usage: offsetscan -dump <path/to/dump.exe> [-patterns <patterns.txt>] [-format console|header|json]")
		fmt.Fprintln(os.Stderr, "")
		fmt.Fprintln(os.Stderr, "Scans a PE memory dump for byte patterns and resolves offsets.")
		fmt.Fprintln(os.Stderr, "")
		flag.PrintDefaults()
		os.Exit(1)
	}

	// Auto-detect patterns.txt location
	if *patternsPath == "" {
		// Try relative to the executable, then common project paths
		candidates := []string{
			"patterns/patterns.txt",
			"../../patterns/patterns.txt",
			filepath.Join(filepath.Dir(*dumpPath), "patterns.txt"),
		}
		for _, c := range candidates {
			if _, err := os.Stat(c); err == nil {
				*patternsPath = c
				break
			}
		}
		if *patternsPath == "" {
			fmt.Fprintln(os.Stderr, "Error: could not find patterns.txt. Use -patterns flag.")
			os.Exit(1)
		}
	}

	// 1. Parse PE dump
	fmt.Fprintf(os.Stderr, "[*] Loading PE dump: %s\n", *dumpPath)
	pe, err := ParsePE(*dumpPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error parsing PE: %v\n", err)
		os.Exit(1)
	}

	fmt.Fprintf(os.Stderr, "[*] ImageBase: 0x%X\n", pe.ImageBase)
	fmt.Fprintf(os.Stderr, "[*] .text section: RVA=0x%X, Size=0x%X (%d MB)\n",
		pe.TextSection.VirtualAddress,
		pe.TextSection.RawSize,
		pe.TextSection.RawSize/1024/1024)
	if pe.IsMemDump {
		fmt.Fprintf(os.Stderr, "[*] Detected as: memory dump\n")
	} else {
		fmt.Fprintf(os.Stderr, "[*] Detected as: on-disk PE\n")
	}

	// 2. Parse patterns
	fmt.Fprintf(os.Stderr, "[*] Loading patterns: %s\n", *patternsPath)
	patterns, err := ParsePatterns(*patternsPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error parsing patterns: %v\n", err)
		os.Exit(1)
	}
	fmt.Fprintf(os.Stderr, "[*] Loaded %d patterns\n", len(patterns))

	// 3. Get .text section data
	textData := pe.TextData()
	textRVA := pe.TextRVA()

	// 4. Scan and resolve
	fmt.Fprintf(os.Stderr, "[*] Scanning...\n")
	var results []ResolvedResult
	var missed []string

	for _, pat := range patterns {
		match := Scan(textData, textRVA, pat)
		if match == nil {
			missed = append(missed, pat.Name)
			if *verbose {
				fmt.Fprintf(os.Stderr, "  [MISS] %s\n", pat.Name)
			}
			continue
		}

		resolved := Resolve(textData, textRVA, *match)
		results = append(results, resolved)

		if *verbose {
			// Check for multiple matches
			allMatches := ScanAll(textData, textRVA, pat)
			if len(allMatches) > 1 {
				fmt.Fprintf(os.Stderr, "  [WARN] %s: %d total matches (using first at RVA 0x%X)\n",
					pat.Name, len(allMatches), match.RVA)
			}

			// Show matched bytes
			bytesHex := ""
			for i, b := range match.MatchedBytes {
				if i > 0 {
					bytesHex += " "
				}
				bytesHex += fmt.Sprintf("%02X", b)
			}
			fmt.Fprintf(os.Stderr, "  [OK]   %-30s RVA=0x%08X -> 0x%X (%s) bytes=[%s]\n",
				pat.Name, match.RVA, resolved.ResolvedOffset, resolved.ClassLabel, bytesHex)
		}
	}

	fmt.Fprintf(os.Stderr, "[*] Done: %d matched, %d missed\n\n", len(results), len(missed))

	// 5. Output
	switch *format {
	case "console":
		FormatConsole(results, missed)
	case "header":
		fmt.Print(FormatHeader(results))
	case "json":
		fmt.Println(FormatJSON(results))
	default:
		fmt.Fprintf(os.Stderr, "Unknown format: %s (use console, header, or json)\n", *format)
		os.Exit(1)
	}
}
