.PHONY: check build-app build-cli-app clean

ARCHS ?= $(shell uname -m)

check:
	cmake -S . -B .build/cmake -DCMAKE_BUILD_TYPE=Debug
	cmake --build .build/cmake --parallel
	ctest --test-dir .build/cmake --output-on-failure

build-app:
	cmake -E remove_directory \
		"$(CURDIR)/.build/xcode-app/Build/Products/Debug/TimeCardMacOS.app"
	xcodebuild -project TimeCardMacOS.xcodeproj -scheme TimeCardMacOS \
		-configuration Debug -derivedDataPath .build/xcode-app \
		ARCHS="$(ARCHS)" ONLY_ACTIVE_ARCH=NO CODE_SIGNING_ALLOWED=NO build
	cmake \
		-DTIMECARD_APP_BUNDLE="$(CURDIR)/.build/xcode-app/Build/Products/Debug/TimeCardMacOS.app" \
		-P Tests/AppBundleTests.cmake

build-cli-app:
	xcodebuild -project TimeCardMacOS.xcodeproj -scheme timecardctl \
		-configuration Debug -derivedDataPath .build/xcode-cli \
		ARCHS="$(ARCHS)" ONLY_ACTIVE_ARCH=NO CODE_SIGNING_ALLOWED=NO build

clean:
	cmake -E remove_directory .build
