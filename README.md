# On Air light

Waits for a client to connect via wifi and lights up RGB led strip according to received messages.
Designed to be run with obs-onair to show whether or not it's safe to open the studio door.

## Hardware

The example device is built from a cheap esp32 board, a piece of a ws2812 rgb led strip and
an extra micro-usb port to get a better position for power input.

The 5 volts and ground of the extra usb port are connected to the VIN and GND pins closest to the
usb port on the board. The led strip is connected to the same two pins plus D13, which is the
data pin right next to the previous two.

## What the light patterns mean

- Rainbow leds: Connecting to wifi
- Off: No client connected
- Yellow: Client connected, OBS not live
- Red spin: Client connected, OBS live
- Red blink: Some sort of error

## Used network protocol

- Built on top of TCP
- All messages 128 or fewer bytes of length
- Any message starting with byte 0 means "off air"
- Any message starting with byte 1 means "on air"
- All other messages are invalid

## To configure and use

First requirement is esp idf development environment. Follow
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/#installation-step-by-step

run `idf.py menuconfig` and set the wifi SSID and password and LED information

run `idf.py monitor` to see what IP the device gets.

Use `nc [ip] [port]` to test that the setup works. The light should turn yellow when connected and
turn off when disconnected.

run `idf.py build` to build the binary.

run `idf.py flash -p [port]` to flash the program on the esp32.

## Development

I feel like this is a good base for all sorts of wifi connected led status lights.
The code is probably not good for live control.
The logic behind the code is as follows:

- common.h defines `state_` enum.
- main.c
 - creates a global state and sets its initial value
  - Yes, it's not the most beautiful solution, but it works for this simple application
 - starts tasks for led control, wifi connection and tcp server
- led task checks the global state and updates leds accordingly on a timer
- wifi connection task tries to connect to the configured wifi network on a loop
- tcp server task waits until wifi is connected and starts a server that accepts one connection
  and handles messages received from that client, setting the state accordingly.

To modify the code for other uses, add/remove states from common.h as needed
(I suggest not removing STARTING, CONNECTING, DISCONNECTED or IDLE),
rewrite `handle_client` in servertask.c to handle your protocol and finally edit
ledtask.h to visualize the states.

