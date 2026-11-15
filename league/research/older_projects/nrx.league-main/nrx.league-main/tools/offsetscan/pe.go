package main

import (
	"encoding/binary"
	"fmt"
	"os"
	"strings"
)

// SectionInfo represents one PE section header.
type SectionInfo struct {
	Name           string
	VirtualAddress uint32
	VirtualSize    uint32
	RawOffset      uint32
	RawSize        uint32
	Characteristics uint32
}

// PEFile holds the parsed PE metadata and raw file bytes.
type PEFile struct {
	Raw         []byte
	ImageBase   uint64
	EntryPoint  uint32
	Sections    []SectionInfo
	TextSection *SectionInfo
	IsMemDump   bool // true if this is a memory dump (sections at VA offsets)
}

// ParsePE reads and parses a PE file from disk.
func ParsePE(path string) (*PEFile, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read file: %w", err)
	}

	if len(data) < 0x40 {
		return nil, fmt.Errorf("file too small to be a PE (%d bytes)", len(data))
	}

	// Check MZ signature
	if data[0] != 'M' || data[1] != 'Z' {
		return nil, fmt.Errorf("invalid MZ signature: %02X %02X", data[0], data[1])
	}

	// Get PE header offset from e_lfanew at 0x3C
	peOffset := binary.LittleEndian.Uint32(data[0x3C:0x40])
	if int(peOffset)+24 > len(data) {
		return nil, fmt.Errorf("PE header offset 0x%X out of bounds", peOffset)
	}

	// Check PE signature
	if data[peOffset] != 'P' || data[peOffset+1] != 'E' || data[peOffset+2] != 0 || data[peOffset+3] != 0 {
		return nil, fmt.Errorf("invalid PE signature at 0x%X", peOffset)
	}

	// COFF header starts at peOffset+4
	coffStart := peOffset + 4
	machine := binary.LittleEndian.Uint16(data[coffStart : coffStart+2])
	numSections := binary.LittleEndian.Uint16(data[coffStart+2 : coffStart+4])
	optHeaderSize := binary.LittleEndian.Uint16(data[coffStart+16 : coffStart+18])

	if machine != 0x8664 {
		return nil, fmt.Errorf("not an x64 PE (machine=0x%X)", machine)
	}

	// Optional header starts at coffStart+20
	optStart := coffStart + 20
	if int(optStart)+2 > len(data) {
		return nil, fmt.Errorf("optional header out of bounds")
	}

	magic := binary.LittleEndian.Uint16(data[optStart : optStart+2])
	if magic != 0x20B {
		return nil, fmt.Errorf("not a PE32+ (magic=0x%X)", magic)
	}

	pe := &PEFile{Raw: data}

	// Entry point RVA at optStart+16
	pe.EntryPoint = binary.LittleEndian.Uint32(data[optStart+16 : optStart+20])

	// ImageBase at optStart+24 (8 bytes for PE32+)
	pe.ImageBase = binary.LittleEndian.Uint64(data[optStart+24 : optStart+32])

	// Section headers start after optional header
	sectionStart := optStart + uint32(optHeaderSize)

	for i := 0; i < int(numSections); i++ {
		off := sectionStart + uint32(i)*40
		if int(off)+40 > len(data) {
			break
		}

		nameBytes := data[off : off+8]
		name := strings.TrimRight(string(nameBytes), "\x00")

		sec := SectionInfo{
			Name:           name,
			VirtualSize:    binary.LittleEndian.Uint32(data[off+8 : off+12]),
			VirtualAddress: binary.LittleEndian.Uint32(data[off+12 : off+16]),
			RawSize:        binary.LittleEndian.Uint32(data[off+16 : off+20]),
			RawOffset:      binary.LittleEndian.Uint32(data[off+20 : off+24]),
			Characteristics: binary.LittleEndian.Uint32(data[off+36 : off+40]),
		}
		pe.Sections = append(pe.Sections, sec)

		if name == ".text" {
			pe.TextSection = &pe.Sections[len(pe.Sections)-1]
		}
	}

	if pe.TextSection == nil {
		return nil, fmt.Errorf("no .text section found")
	}

	// Detect if this is a memory dump or on-disk PE.
	// In a memory dump, sections are stored at file offset == VirtualAddress.
	// In an on-disk PE, PointerToRawData is typically much smaller than VirtualAddress.
	pe.IsMemDump = pe.detectMemDump()

	return pe, nil
}

func (p *PEFile) detectMemDump() bool {
	// If most sections have RawOffset == VirtualAddress, it's an on-disk PE
	// that was built normally. If RawOffset != VirtualAddress but the file is
	// large enough to hold data at VA offsets, check which one has valid data.
	for _, s := range p.Sections {
		if s.RawOffset == 0 || s.VirtualAddress == 0 {
			continue
		}
		if s.RawOffset == s.VirtualAddress {
			continue // ambiguous, could be either
		}
		// If data at VirtualAddress offset in the file is non-zero, it's a mem dump
		if uint64(s.VirtualAddress)+uint64(s.VirtualSize) <= uint64(len(p.Raw)) {
			chunk := p.Raw[s.VirtualAddress : s.VirtualAddress+min(s.VirtualSize, 64)]
			nonZero := 0
			for _, b := range chunk {
				if b != 0 {
					nonZero++
				}
			}
			if nonZero > 0 {
				return false // data found at both, use RawOffset (on-disk PE behavior)
			}
		}
		// Data is at RawOffset, not VirtualAddress — on-disk PE
		return false
	}
	return false
}

// TextData returns the raw bytes of the .text section.
func (p *PEFile) TextData() []byte {
	s := p.TextSection
	offset := s.RawOffset
	size := s.RawSize

	// Use the larger of VirtualSize and RawSize for scanning
	if s.VirtualSize > size {
		size = s.VirtualSize
	}

	end := uint64(offset) + uint64(size)
	if end > uint64(len(p.Raw)) {
		end = uint64(len(p.Raw))
	}

	return p.Raw[offset:end]
}

// TextRVA returns the RVA where the .text section starts.
func (p *PEFile) TextRVA() uint32 {
	return p.TextSection.VirtualAddress
}
