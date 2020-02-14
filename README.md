ADEiger
===========
An
[EPICS](http://www.aps.anl.gov/epics/)
[areaDetector](https://cars.uchicago.edu/software/epics/areaDetector.html)
driver for the Eiger pixel array detectors from
[Dectris](http://www.dectris.com).

Additional information:
* [Documentation](https://areadetector.github.io/master/ADEiger/eiger.html)
* [Release notes](RELEASE.md).

Notes:
------

* Depends on the Eiger having the firmware 1.6.4 or newer.
* Depends on ADCore R2-6 or newer.
* Currently this was only tested on Linux 64-bit machines.

Before compiling:
-----------------

* Set `ZMQ` variables inside `ADEiger/configure/CONFIG_SITE` to point to a ZeroMQ installation location if a system package is not installed (`libzmq3-dev` on Debian systems).
  - ZeroMQ 4 is required

HDF5 Plugins:
-------------

If you want HDF5 tools to compress files produced by the detector, you will need to have the bitshuffle and LZ4 decompression
plugins available.  These are built in ADSupport.

The environment variable `HDF5_PLUGIN_PATH` should be set to `[your_path]/ADSupport/lib/linux-x86_64`.

How to run:
-----------

* Under `ADEiger/iocs/eigerIOC/iocBoot/iocEiger/` there is already a ready to use IOC:
  - Change the IP address in `st.cmd`.
  - Run `./st.cmd`.
* There are medm, edm, CSS-Boy, caQtDM, and Phoebus Display Builder screens under `areaDetector/ADEiger/eigerApp/op/`.

