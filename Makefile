# SPDX-License-Identifier: MIT
#
# Shortcuts for building, testing and packaging Clauds for KDE Plasma (kde/).
#
#   make            configure and build
#   make test       run the test suites
#   make deb        build the .deb
#   make install    build the .deb and install it with apt
#   make help       list every target
#
# Settings (override on the command line, for example `make BUILD_TYPE=Debug`):
#   BUILD_DIR   build folder                      (default: build-kde)
#   BUILD_TYPE  CMake build type                  (default: RelWithDebInfo)
#   PREFIX      install prefix                    (default: /usr)
#   KF6         ON/OFF, use KDE Frameworks 6      (default: ON)
#   APP         ON/OFF, build the tray app        (default: ON)
#   JOBS        parallel build jobs               (default: all CPUs)
#   CMAKE_ARGS  extra arguments for the configure step

BUILD_DIR  ?= build-kde
BUILD_TYPE ?= RelWithDebInfo
PREFIX     ?= /usr
KF6        ?= ON
APP        ?= ON
JOBS       ?= $(shell nproc 2>/dev/null || echo 2)
CMAKE_ARGS ?=

CMAKE ?= cmake
CTEST ?= ctest
CPACK ?= cpack
SUDO  ?= sudo

SRC_DIR := kde
BINARY  := $(BUILD_DIR)/src/app/clauds

APT_BUILD_DEPS := cmake g++ make file dpkg-dev extra-cmake-modules \
	qt6-base-dev qt6-declarative-dev qt6-webengine-dev libgl-dev \
	libkf6statusnotifieritem-dev libkf6notifications-dev libkf6coreaddons-dev libkf6colorscheme-dev \
	qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-org-kde-desktop

.DEFAULT_GOAL := build

.PHONY: help
help: ## List the targets
	@awk 'BEGIN {FS = ":.*## "} /^[a-zA-Z_-]+:.*## / {printf "  %-16s %s\n", $$1, $$2}' $(MAKEFILE_LIST)

.PHONY: deps
deps: ## Install the build dependencies (Kubuntu, uses sudo apt)
	$(SUDO) apt install $(APT_BUILD_DEPS)

.PHONY: configure
configure: ## Run CMake (always; `build` only does it when needed)
	$(CMAKE) -S $(SRC_DIR) -B $(BUILD_DIR) \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DCLAUDS_WITH_KF6=$(KF6) \
		-DCLAUDS_BUILD_APP=$(APP) \
		$(CMAKE_ARGS)

$(BUILD_DIR)/CMakeCache.txt:
	$(MAKE) configure

.PHONY: build
build: $(BUILD_DIR)/CMakeCache.txt ## Build the app and the tests
	$(CMAKE) --build $(BUILD_DIR) -j $(JOBS)

.PHONY: test
test: build ## Build, then run the test suites
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

.PHONY: run
run: build ## Build, then start Clauds from the build folder
	$(BINARY)

.PHONY: deb
deb: build ## Build the .deb package in $(BUILD_DIR)
	rm -f $(BUILD_DIR)/clauds-*.deb
	cd $(BUILD_DIR) && $(CPACK) -G DEB

.PHONY: install
install: deb ## Build the .deb and install it (uses sudo apt)
	$(SUDO) apt install ./$(BUILD_DIR)/clauds-*.deb

.PHONY: uninstall
uninstall: ## Remove the installed package (uses sudo apt)
	$(SUDO) apt remove clauds

.PHONY: clean
clean: ## Delete the build folder
	rm -rf $(BUILD_DIR)

# ---- Flatpak (see kde/packaging/flatpak) ----

FLATPAK_ID       := io.github.multiclauds.clauds-kde
FLATPAK_MANIFEST := kde/packaging/flatpak/$(FLATPAK_ID).yml
FLATPAK_BUILD    ?= build-flatpak
FLATPAK_REPO     ?= $(FLATPAK_BUILD)/repo
FLATPAK_BUNDLE   ?= $(FLATPAK_BUILD)/$(FLATPAK_ID).flatpak
FLATPAK_BRANCH   ?= 6.10
FLATPAK_BUILDER  ?= flatpak-builder

.PHONY: flatpak-runtime
flatpak-runtime: ## Install only the KDE runtime, SDK and Qt WebEngine base (no sudo, for CI)
	flatpak install --user --noninteractive flathub \
		org.kde.Platform//$(FLATPAK_BRANCH) org.kde.Sdk//$(FLATPAK_BRANCH) \
		io.qt.qtwebengine.BaseApp//$(FLATPAK_BRANCH)

.PHONY: flatpak-deps
flatpak-deps: ## Install flatpak-builder, the KDE runtime, SDK and Qt WebEngine base (sudo apt, flathub)
	$(SUDO) apt install flatpak flatpak-builder
	$(MAKE) flatpak-runtime

.PHONY: flatpak
flatpak: ## Build the Flatpak and write build-flatpak/io.github.multiclauds.clauds-kde.flatpak
	$(FLATPAK_BUILDER) --user --force-clean --state-dir=$(FLATPAK_BUILD)/state \
		--repo=$(FLATPAK_REPO) $(FLATPAK_BUILD)/app $(FLATPAK_MANIFEST)
	flatpak build-bundle $(FLATPAK_REPO) $(FLATPAK_BUNDLE) $(FLATPAK_ID)

.PHONY: flatpak-install
flatpak-install: flatpak ## Build the Flatpak and install it for your user
	flatpak install --user --noninteractive --reinstall $(FLATPAK_BUNDLE)

.PHONY: flatpak-run
flatpak-run: ## Start the installed Flatpak
	flatpak run $(FLATPAK_ID)

.PHONY: flatpak-uninstall
flatpak-uninstall: ## Remove the installed Flatpak (keeps your data folders)
	flatpak uninstall --user --noninteractive $(FLATPAK_ID)
