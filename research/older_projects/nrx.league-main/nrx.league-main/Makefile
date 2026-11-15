# ============================================================================
# nrx.league Makefile
# Wraps MSBuild for convenient command-line builds
# ============================================================================

# --- Auto-detect MSBuild ---
# VS 2025 (v18)
MSBUILD_V18 := C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe
# VS 2022 (v17)
MSBUILD_V17 := C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe
MSBUILD_V17_PRO := C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe
MSBUILD_V17_ENT := C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe

# Pick the first one that exists
ifneq (,$(wildcard $(MSBUILD_V18)))
    MSBUILD := $(MSBUILD_V18)
else ifneq (,$(wildcard $(MSBUILD_V17)))
    MSBUILD := $(MSBUILD_V17)
else ifneq (,$(wildcard $(MSBUILD_V17_PRO)))
    MSBUILD := $(MSBUILD_V17_PRO)
else ifneq (,$(wildcard $(MSBUILD_V17_ENT)))
    MSBUILD := $(MSBUILD_V17_ENT)
else
    MSBUILD := MSBuild.exe
endif

SOLUTION   := leagueone.sln
BUILD_DIR  := $(CURDIR)

# Default target
.PHONY: all
all: release

# ============================================================================
# Primary Build Targets
# ============================================================================

.PHONY: release
release: ## Build Release|x64 DLL (default)
	@echo [BUILD] Release x64
	"$(MSBUILD)" "$(SOLUTION)" /p:Configuration=Release /p:Platform=x64 /t:Build /m /nologo /v:minimal

.PHONY: debug
debug: ## Build Debug|x64 DLL
	@echo [BUILD] Debug x64
	"$(MSBUILD)" "$(SOLUTION)" /p:Configuration=Debug /p:Platform=x64 /t:Build /m /nologo /v:minimal

.PHONY: release-x86
release-x86: ## Build Release|Win32 EXE
	@echo [BUILD] Release Win32
	"$(MSBUILD)" "$(SOLUTION)" /p:Configuration=Release /p:Platform=Win32 /t:Build /m /nologo /v:minimal

.PHONY: debug-x86
debug-x86: ## Build Debug|Win32 EXE
	@echo [BUILD] Debug Win32
	"$(MSBUILD)" "$(SOLUTION)" /p:Configuration=Debug /p:Platform=Win32 /t:Build /m /nologo /v:minimal

# ============================================================================
# Build All Configurations
# ============================================================================

.PHONY: all-configs
all-configs: release debug release-x86 debug-x86 ## Build all 4 configurations

# ============================================================================
# Clean Targets
# ============================================================================

.PHONY: clean
clean: ## Clean Release|x64
	@echo [CLEAN] Release x64
	"$(MSBUILD)" "$(SOLUTION)" /p:Configuration=Release /p:Platform=x64 /t:Clean /m /nologo /v:minimal

.PHONY: clean-debug
clean-debug: ## Clean Debug|x64
	@echo [CLEAN] Debug x64
	"$(MSBUILD)" "$(SOLUTION)" /p:Configuration=Debug /p:Platform=x64 /t:Clean /m /nologo /v:minimal

.PHONY: clean-all
clean-all: ## Clean all configurations and build artifacts
	@echo [CLEAN] All configurations
	"$(MSBUILD)" "$(SOLUTION)" /p:Configuration=Release /p:Platform=x64 /t:Clean /m /nologo /v:quiet
	"$(MSBUILD)" "$(SOLUTION)" /p:Configuration=Debug /p:Platform=x64 /t:Clean /m /nologo /v:quiet
	"$(MSBUILD)" "$(SOLUTION)" /p:Configuration=Release /p:Platform=Win32 /t:Clean /m /nologo /v:quiet
	"$(MSBUILD)" "$(SOLUTION)" /p:Configuration=Debug /p:Platform=Win32 /t:Clean /m /nologo /v:quiet
	@echo [CLEAN] Removing output directories
	-rm -rf x64 Debug Release

# ============================================================================
# Rebuild (Clean + Build)
# ============================================================================

.PHONY: rebuild
rebuild: ## Rebuild Release|x64 (clean + build)
	@echo [REBUILD] Release x64
	"$(MSBUILD)" "$(SOLUTION)" /p:Configuration=Release /p:Platform=x64 /t:Rebuild /m /nologo /v:minimal

.PHONY: rebuild-debug
rebuild-debug: ## Rebuild Debug|x64 (clean + build)
	@echo [REBUILD] Debug x64
	"$(MSBUILD)" "$(SOLUTION)" /p:Configuration=Debug /p:Platform=x64 /t:Rebuild /m /nologo /v:minimal

# ============================================================================
# Utility
# ============================================================================

.PHONY: info
info: ## Show build environment info
	@echo =============================================
	@echo  nrx.league Build Info
	@echo =============================================
	@echo  MSBuild: $(MSBUILD)
	@echo  Solution: $(SOLUTION)
	@echo  Build Dir: $(BUILD_DIR)
	@echo =============================================
	@echo.
	@echo  Targets:
	@echo    make              - Build Release x64 DLL
	@echo    make debug        - Build Debug x64 DLL
	@echo    make release-x86  - Build Release Win32 EXE
	@echo    make debug-x86    - Build Debug Win32 EXE
	@echo    make all-configs  - Build all 4 configurations
	@echo    make clean        - Clean Release x64
	@echo    make clean-all    - Clean everything
	@echo    make rebuild      - Rebuild Release x64
	@echo    make info         - Show this info
	@echo =============================================

.PHONY: help
help: info ## Alias for info
