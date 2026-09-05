.PHONY: check build-app build-cli-app clean

ARCHS ?= $(shell uname -m)

check:
	cmake -S . -B .build/cmake -DCMAKE_BUILD_TYPE=Debug
	cmake --build .build/cmake --parallel
	ctest --test-dir .build/cmake --output-on-failure
	cmake -E make_directory .build/cmake/swift-module-cache
	xcrun swiftc -parse-as-library -swift-version 5 \
		-module-cache-path .build/cmake/swift-module-cache \
		App/TimeCardClient.swift App/MotionModels.swift Tests/TimeCardClientTests.swift \
		-framework IOKit -framework CoreFoundation \
		-o .build/cmake/timecard-control-center-model-tests
	.build/cmake/timecard-control-center-model-tests
	xcrun swiftc -parse-as-library -swift-version 5 \
		-module-cache-path .build/cmake/swift-module-cache \
		App/TimeCardClient.swift App/MotionModels.swift App/ConfigurationProfiles.swift App/ConfigurationProfileClient.swift \
		Tests/ConfigurationProfileTests.swift \
		-framework IOKit -framework CoreFoundation \
		-o .build/cmake/timecard-profile-tests
	.build/cmake/timecard-profile-tests
	xcrun swiftc -parse-as-library -swift-version 5 \
		-module-cache-path .build/cmake/swift-module-cache \
		App/TimeCardClient.swift App/MotionModels.swift App/AtomicClockModels.swift App/AtomicClockClient.swift \
		Tests/PeripheralTests.swift -framework IOKit -framework CoreFoundation \
		-o .build/cmake/timecard-peripheral-tests
	.build/cmake/timecard-peripheral-tests
	xcrun swiftc -parse-as-library -swift-version 5 \
		-module-cache-path .build/cmake/swift-module-cache \
		App/ReceiverModels.swift App/ReceiverCapture.swift App/TelemetryModels.swift \
		Tests/ReceiverTelemetryTests.swift \
		-o .build/cmake/timecard-receiver-telemetry-tests
	.build/cmake/timecard-receiver-telemetry-tests

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
