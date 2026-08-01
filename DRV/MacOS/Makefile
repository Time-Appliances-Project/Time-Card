.PHONY: check build-app clean

check:
	cmake -S . -B .build/cmake -DCMAKE_BUILD_TYPE=Debug
	cmake --build .build/cmake --parallel
	ctest --test-dir .build/cmake --output-on-failure

build-app:
	xcodebuild -project TimeCardMacOS.xcodeproj -scheme TimeCardMacOS \
		-configuration Debug -derivedDataPath .build/xcode-app \
		CODE_SIGNING_ALLOWED=NO build

clean:
	cmake -E remove_directory .build
