.PHONY: build flash monitor menuconfig clean fullclean size

# Serial port — override with: make flash PORT=COM5
PORT ?= /dev/ttyUSB0

# Detect OS
ifeq ($(OS),Windows_NT)
SHELL := pwsh.exe
endif

build:
	idf.py build

flash:
	idf.py -p $(PORT) flash

monitor:
	idf.py -p $(PORT) monitor

menuconfig:
	idf.py menuconfig

clean:
	idf.py clean

fullclean:
	idf.py fullclean

size:
	idf.py size

size-components:
	idf.py size-components
