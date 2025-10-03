# Morse Micro Smart Manager

## About

Morse Micro Smart Manager is an extensible application and framework enabling
intelligent control of Morse Micro Wi-Fi HaLow features such as dynamic channel
selection.

## Dependencies

Smart manager requires libconfig to run. It will be dynamically linked when started

## Building

Smart Manager uses the scons build system on Linux.

If necessary, it can be installed on most Linux systems with the package
management framework, e.g. on Ubuntu:

``` shell
$ sudo apt install scons
```

Simply run `scons` to build Smart Manager.

## Running

The Smart Manager executable can be found in the build directory and can
be run directly.

## Example application

The main example application for Smart Manager is
`src/smart_manager_main.c`. This application demonstrates how to connect
to hostapd using the control interface, set up a polling monitor, and
request some data.

This may be customised with code to call Smart Manager functions to
access the hostapd control interface.

Any command that would be accepted by hostapd can be passed here.
