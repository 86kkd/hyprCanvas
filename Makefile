.PHONY: all debug release clean install dev

debug:
	cmake -B ./Debug -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$(PREFIX)
	cmake --build ./Debug --clean-first -j
	rm -rf ./compile_commands.json
	rm -rf ./karta.so
	ln -s ./Debug/compile_commands.json .
	ln -s ./Debug/karta.so .

release:
	cmake -B ./Release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(PREFIX)
	cmake --build ./Release --clean-first -j
	rm -rf ./compile_commands.json
	rm -rf ./karta.so
	ln -s ./Release/compile_commands.json .
	ln -s ./Release/karta.so .

all: clean release

clean:
	rm -rf Release
	rm -rf Debug
	rm -rf ./karta.so
	rm -rf ./compile_commands.json

install: release
	mkdir -p `xdg-user-dir`/.config/hypr/plugins
	cp karta.so `xdg-user-dir`/.config/hypr/plugins

dev: clean debug
