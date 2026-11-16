package main

import (
	"encoding/binary"
)

// OffsetClass categorizes how a pattern match should be resolved.
type OffsetClass int

const (
	ClassUnknown       OffsetClass = iota
	ClassRIPRelative               // RIP-relative: resolved = matchRVA + instrLen + disp32
	ClassStructOffset              // Struct member offset: the displacement IS the offset
	ClassFunctionEntry             // Function prologue: the match RVA IS the offset
)

// ResolvedResult holds the output of resolving a single pattern match.
type ResolvedResult struct {
	Match          Match
	Class          OffsetClass
	ClassLabel     string // Human-readable label
	DispOffset     int    // Where in the matched bytes the displacement starts
	InstrLen       int    // Total instruction length (displacement end)
	DispSize       int    // Displacement size in bytes (1 for disp8, 4 for disp32)
	Displacement   int32  // The signed 32-bit displacement value
	ResolvedOffset uint32 // Final resolved offset
	DispBytes      []byte // Raw displacement bytes for display
}

// Resolve analyzes a match and resolves the offset.
// textData is the full .text section bytes, textRVA is where .text starts.
func Resolve(textData []byte, textRVA uint32, m Match) ResolvedResult {
	r := ResolvedResult{Match: m}

	// Get the actual bytes from the dump (not just the pattern)
	dataOffset := int(m.OffsetInText)
	remaining := len(textData) - dataOffset
	if remaining < 2 {
		r.Class = ClassUnknown
		r.ClassLabel = "Too short"
		return r
	}

	// Read up to 16 bytes from the match location for classification
	readLen := 16
	if remaining < readLen {
		readLen = remaining
	}
	bytes := textData[dataOffset : dataOffset+readLen]

	class, label, dispOff, instrLen, dispSize := classifyInstruction(bytes)
	r.Class = class
	r.ClassLabel = label
	r.DispOffset = dispOff
	r.InstrLen = instrLen
	r.DispSize = dispSize

	switch class {
	case ClassRIPRelative:
		if dataOffset+dispOff+4 <= len(textData) {
			dispBytes := textData[dataOffset+dispOff : dataOffset+dispOff+4]
			r.DispBytes = append([]byte(nil), dispBytes...)
			r.Displacement = int32(binary.LittleEndian.Uint32(dispBytes))
			// RIP-relative: target = (matchRVA + instrLen) + displacement
			nextInstrRVA := m.RVA + uint32(instrLen)
			r.ResolvedOffset = uint32(int64(nextInstrRVA) + int64(r.Displacement))
		}

	case ClassStructOffset:
		if dispSize == 1 && dataOffset+dispOff+1 <= len(textData) {
			// disp8 (single byte displacement)
			r.DispBytes = []byte{textData[dataOffset+dispOff]}
			r.Displacement = int32(int8(textData[dataOffset+dispOff]))
			r.ResolvedOffset = uint32(r.Displacement)
		} else if dataOffset+dispOff+4 <= len(textData) {
			// disp32 (four byte displacement)
			dispBytes := textData[dataOffset+dispOff : dataOffset+dispOff+4]
			r.DispBytes = append([]byte(nil), dispBytes...)
			r.Displacement = int32(binary.LittleEndian.Uint32(dispBytes))
			r.ResolvedOffset = uint32(r.Displacement)
		}

	case ClassFunctionEntry:
		r.ResolvedOffset = m.RVA
	}

	return r
}

// classifyInstruction determines the instruction type from the actual bytes.
// Returns: class, label, displacement offset, instruction length, displacement size (1 or 4)
func classifyInstruction(b []byte) (OffsetClass, string, int, int, int) {
	if len(b) < 2 {
		return ClassUnknown, "Unknown", 0, 0, 0
	}

	// --- CALL rel32 ---
	if b[0] == 0xE8 {
		return ClassRIPRelative, "CALL rel32", 1, 5, 4
	}

	// --- JMP rel32 ---
	if b[0] == 0xE9 {
		return ClassRIPRelative, "JMP rel32", 1, 5, 4
	}

	// --- FF 15: CALL [rip+disp32] (indirect call via IAT) ---
	if b[0] == 0xFF && len(b) > 1 && b[1] == 0x15 {
		return ClassRIPRelative, "CALL [rip+disp32]", 2, 6, 4
	}

	// --- C6 05: MOV byte [rip+disp32], imm8 ---
	if b[0] == 0xC6 && len(b) > 1 && b[1] == 0x05 {
		return ClassRIPRelative, "MOV [rip+disp32], imm8", 2, 7, 4
	}

	// --- C6 84: MOV byte [rsp+reg+disp32], imm8 --- (SIB byte at [2])
	if b[0] == 0xC6 && len(b) > 2 && b[1] == 0x84 {
		// C6 84 SIB disp32 imm8: dispOffset=3, instrLen=8, disp32
		return ClassStructOffset, "MOV [rsp+reg+disp32], imm8", 3, 8, 4
	}

	// --- 44 8B 05/0D: MOV r32, [rip+disp32] (REX.R prefix) ---
	if b[0] == 0x44 && len(b) > 2 && b[1] == 0x8B && isRIPModRM(b[2]) {
		return ClassRIPRelative, "MOV r32d, [rip+disp32]", 3, 7, 4
	}

	// --- 44 8B with mod=10: MOV r32, [reg+disp32] ---
	if b[0] == 0x44 && len(b) > 2 && b[1] == 0x8B {
		modrm := b[2]
		mod := (modrm >> 6) & 0x03
		rm := modrm & 0x07
		if mod == 0x01 {
			dispOff := 3
			if rm == 0x04 {
				dispOff = 4
			}
			return ClassStructOffset, "MOV r32, [reg+disp8]", dispOff, dispOff + 1, 1
		}
	}

	// --- 0F B6/B7: MOVZX with ModR/M ---
	if b[0] == 0x0F && len(b) > 2 && (b[1] == 0xB6 || b[1] == 0xB7) {
		modrm := b[2]
		mod := (modrm >> 6) & 0x03
		rm := modrm & 0x07
		if mod == 0x02 { // [reg+disp32]
			dispOff := 3
			if rm == 0x04 { // SIB
				dispOff = 4
			}
			return ClassStructOffset, "MOVZX [reg+disp32]", dispOff, dispOff + 4, 4
		}
		if mod == 0x01 { // [reg+disp8]
			dispOff := 3
			if rm == 0x04 { // SIB
				dispOff = 4
			}
			return ClassStructOffset, "MOVZX [reg+disp8]", dispOff, dispOff + 1, 1
		}
	}

	// --- 8B: MOV with ModR/M ---
	if b[0] == 0x8B && len(b) > 1 {
		modrm := b[1]
		mod := (modrm >> 6) & 0x03
		rm := modrm & 0x07
		if mod == 0x02 {
			dispOff := 2
			if rm == 0x04 {
				dispOff = 3
			}
			return ClassStructOffset, "MOV reg, [reg+disp32]", dispOff, dispOff + 4, 4
		}
		if mod == 0x01 {
			dispOff := 2
			if rm == 0x04 {
				dispOff = 3
			}
			return ClassStructOffset, "MOV reg, [reg+disp8]", dispOff, dispOff + 1, 1
		}
	}

	// --- F3 prefix (SSE) ---
	if b[0] == 0xF3 && len(b) > 3 && b[1] == 0x0F {
		modrm := b[3]
		mod := (modrm >> 6) & 0x03
		rm := modrm & 0x07

		if mod == 0x00 && rm == 0x05 {
			return ClassRIPRelative, "SSE [rip+disp32]", 4, 8, 4
		}
		if mod == 0x02 {
			dispOff := 4
			if rm == 0x04 {
				dispOff = 5
			}
			return ClassStructOffset, "SSE [reg+disp32]", dispOff, dispOff + 4, 4
		}
		if mod == 0x01 {
			dispOff := 4
			if rm == 0x04 {
				dispOff = 5
			}
			return ClassStructOffset, "SSE [reg+disp8]", dispOff, dispOff + 1, 1
		}
	}

	// --- REX.W prefix (0x48-0x4F) ---
	if b[0] >= 0x48 && b[0] <= 0x4F && len(b) > 2 {
		rex := b[0]
		opcode := b[1]
		modrm := b[2]
		mod := (modrm >> 6) & 0x03
		rm := modrm & 0x07

		switch opcode {
		case 0x8B: // MOV reg, [...]
			if mod == 0x00 && rm == 0x05 {
				return ClassRIPRelative, "MOV reg, [rip+disp32]", 3, 7, 4
			}
			if mod == 0x02 {
				dispOff := 3
				if rm == 0x04 {
					dispOff = 4
				}
				return ClassStructOffset, "MOV reg, [reg+disp32]", dispOff, dispOff + 4, 4
			}
			if mod == 0x01 {
				dispOff := 3
				if rm == 0x04 {
					dispOff = 4
				}
				return ClassStructOffset, "MOV reg, [reg+disp8]", dispOff, dispOff + 1, 1
			}

		case 0x89: // MOV [...], reg
			if mod == 0x00 && rm == 0x05 {
				return ClassRIPRelative, "MOV [rip+disp32], reg", 3, 7, 4
			}
			// mod=01 with 0x89 at the start of a pattern is almost always a function prologue
			// e.g., 48 89 5C 24 18 (MOV [rsp+0x18], rbx) or 48 89 48 08 (MOV [rax+8], rcx)
			if mod == 0x01 {
				return ClassFunctionEntry, "MOV [reg+disp8], reg (prologue)", 0, 0, 0
			}

		case 0x8D: // LEA reg, [...]
			if mod == 0x00 && rm == 0x05 {
				return ClassRIPRelative, "LEA reg, [rip+disp32]", 3, 7, 4
			}
			if mod == 0x02 {
				dispOff := 3
				if rm == 0x04 {
					dispOff = 4
				}
				return ClassStructOffset, "LEA reg, [reg+disp32]", dispOff, dispOff + 4, 4
			}

		case 0x81: // ADD/SUB/CMP reg, imm32
			return ClassStructOffset, "ADD/SUB reg, imm32", 3, 7, 4

		case 0x83: // ADD/SUB/CMP reg, imm8 or [mem+disp], imm8
			if mod == 0x02 {
				// CMP qword [reg+disp32], imm8
				dispOff := 3
				if rm == 0x04 {
					dispOff = 4
				}
				return ClassStructOffset, "CMP/ADD [reg+disp32], imm8", dispOff, dispOff + 4 + 1, 4
			}
			if mod == 0x03 {
				// SUB rsp, imm8 (function prologue)
				return ClassFunctionEntry, "SUB rsp, imm8 (prologue)", 0, 0, 0
			}

		case 0x85: // TEST reg, reg
			return ClassFunctionEntry, "TEST (prologue context)", 0, 0, 0
		}

		// Handle LEA with various REX prefixes
		if opcode == 0x8D {
			if mod == 0x00 && rm == 0x05 {
				return ClassRIPRelative, "LEA reg, [rip+disp32]", 3, 7, 4
			}
			if mod == 0x02 {
				dispOff := 3
				if rm == 0x04 {
					dispOff = 4
				}
				return ClassStructOffset, "LEA reg, [reg+disp32]", dispOff, dispOff + 4, 4
			}
		}

		// Handle MOV with REX.WR (4C 8B)
		if rex == 0x4C && opcode == 0x8B {
			if mod == 0x00 && rm == 0x05 {
				return ClassRIPRelative, "MOV r64, [rip+disp32]", 3, 7, 4
			}
		}

		// Remaining REX.W + opcode combinations not handled above
		// fall through to function prologue checks below
	}

	// --- Function prologues ---
	if b[0] == 0x40 && len(b) > 1 && b[1] == 0x53 {
		return ClassFunctionEntry, "PUSH rbx (prologue)", 0, 0, 0
	}

	if b[0] == 0x55 {
		return ClassFunctionEntry, "PUSH rbp (prologue)", 0, 0, 0
	}

	// 73: JNB (short jump) — used as inline anchor
	if b[0] == 0x73 {
		return ClassFunctionEntry, "JNB (inline pattern)", 0, 0, 0
	}

	// 5B C3: pop rbx; ret — function tail anchor
	if b[0] == 0x5B && len(b) > 1 && b[1] == 0xC3 {
		return ClassFunctionEntry, "POP rbx; RET (function tail)", 0, 0, 0
	}

	return ClassUnknown, "Unknown", 0, 0, 0
}

// isRIPModRM returns true if the ModR/M byte encodes [rip+disp32].
// That's mod=00, rm=101 (0b00_xxx_101), so byte & 0xC7 == 0x05.
func isRIPModRM(modrm byte) bool {
	return (modrm & 0xC7) == 0x05
}
