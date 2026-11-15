package main

// Match represents a pattern match in the .text section.
type Match struct {
	PatternName  string
	OffsetInText uint32 // Byte offset within the .text section data
	RVA          uint32 // RVA = TextSection.VirtualAddress + OffsetInText
	MatchedBytes []byte // The actual bytes at the match location
}

// Scan searches the .text section data for a pattern.
// Returns the first match, or nil if not found.
func Scan(textData []byte, textRVA uint32, pat Pattern) *Match {
	patLen := len(pat.Bytes)
	if patLen == 0 || patLen > len(textData) {
		return nil
	}

	limit := len(textData) - patLen
	for i := 0; i <= limit; i++ {
		if matchAt(textData, i, pat.Bytes) {
			// Grab some extra context bytes for display
			end := i + patLen + 8
			if end > len(textData) {
				end = len(textData)
			}
			return &Match{
				PatternName:  pat.Name,
				OffsetInText: uint32(i),
				RVA:          textRVA + uint32(i),
				MatchedBytes: append([]byte(nil), textData[i:end]...),
			}
		}
	}
	return nil
}

// ScanAll returns all matches for a pattern (useful for diagnostics).
func ScanAll(textData []byte, textRVA uint32, pat Pattern) []Match {
	patLen := len(pat.Bytes)
	if patLen == 0 || patLen > len(textData) {
		return nil
	}

	var results []Match
	limit := len(textData) - patLen
	for i := 0; i <= limit; i++ {
		if matchAt(textData, i, pat.Bytes) {
			end := i + patLen + 8
			if end > len(textData) {
				end = len(textData)
			}
			results = append(results, Match{
				PatternName:  pat.Name,
				OffsetInText: uint32(i),
				RVA:          textRVA + uint32(i),
				MatchedBytes: append([]byte(nil), textData[i:end]...),
			})
		}
	}
	return results
}

func matchAt(data []byte, offset int, pattern []int16) bool {
	for j, p := range pattern {
		if p != -1 && data[offset+j] != byte(p) {
			return false
		}
	}
	return true
}
