.PHONY: all debug release clean install dev

debug:
	cmake -B ./Debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$(PREFIX)
	cmake --build ./Debug --clean-first -j
	rm -rf ./compile_commands.json
	rm -rf ./hyprCanvas.so
	ln -s ./Debug/compile_commands.json .
	ln -s ./Debug/hyprCanvas.so .

release:
	cmake -B ./Release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(PREFIX)
	cmake --build ./Release --clean-first -j
	rm -rf ./compile_commands.json
	rm -rf ./hyprCanvas.so
	ln -s ./Release/compile_commands.json .
	ln -s ./Release/hyprCanvas.so .

all: clean release

clean:
	rm -rf Release
	rm -rf Debug
	rm -rf ./hyprCanvas.so
	rm -rf ./compile_commands.json

install: release
	mkdir -p `xdg-user-dir`/.config/hypr/plugins
	cp hyprCanvas.so `xdg-user-dir`/.config/hypr/plugins

dev: clean debug
