ADEiger
===========
An
[EPICS](http://www.aps.anl.gov/epics/)
[areaDetector](http://cars.uchicago.edu/software/epics/areaDetector.html)
driver for the Eiger pixel array detectors from
[Dectris](http://www.dectris.com).

Additional information:
* [Release notes and links to source and binary releases](RELEASE.md).

Notes:
------

* Depends on the Eiger having the firmware 1.6.2 or newer.
* Currently this was only tested on Linux 64-bit machines.

How to install:
---------------

* Clone and compile [areaDetector](https://github.com/areaDetector/areaDetector) according to its instructions.
* Clone this repository.
* Edit `AREADETECTOR` variable inside `ADEiger/configure/RELEASE` to point to the correct location.
* Set `ZMQ` variables inside `ADEiger/configure/CONFIG_SITE` to point to a ZeroMQ installation location if a system package is not installed (`libzmq3-dev` on Debian systems).
  - ZeroMQ 4 is required
* Run `make` inside `ADEiger`.

HDF5 Plugins:
-------------

If you want ADEiger to decode compressed data produced by the detector, you will have to have a couple of decompression
plugins compiled and on the path:

* Bitshuffle:

Available [here](https://github.com/kiyo-masui/bitshuffle).

* LZ4:

Deprecated version available [here](https://github.com/michaelrissi/HDF5Plugin.git).
New version available [here](https://github.com/nexusformat/HDF5-External-Filter-Plugins).

Once they are compiled, add their paths to the environment variable `HDF5_PLUGIN_PATH`, separated by a colon.

How to run:
-----------

* Under `ADEiger/iocs/eigerIOC/iocBoot/iocEiger/` there is already a ready to use IOC:
  - If there is no `envPaths`, run `make envPaths` there.
  - Fix the IP address in `st.cmd`.
  - Run `./st.cmd`.
* There is a CSS screen under `areaDetector/ADEiger/eigerApp/op/opi`, just open it with Controls System Studio.
