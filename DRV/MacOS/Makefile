.PHONY: check build-app build-cli-app clean

ARCHS ?= $(shell uname -m)

check:
	cmake -S . -B .build/cmake -DCMAKE_BUILD_TYPE=Debug
	cmake --build .build/cmake --parallel
	ctest --test-dir .build/cmake --output-on-failure

build-app:
	xcodebuild -project TimeCardMacOS.xcodeproj -scheme TimeCardMacOS \
		-configuration Debug -derivedDataPath .build/xcode-app \
		CODE_SIGNING_ALLOWED=NO build

build-cli-app:
	xcodebuild -project TimeCardMacOS.xcodeproj -scheme timecardctl \
		-configuration Debug -derivedDataPath .build/xcode-cli \
		ARCHS="$(ARCHS)" ONLY_ACTIVE_ARCH=NO CODE_SIGNING_ALLOWED=NO build

clean:
	cmake -E remove_directory .build
