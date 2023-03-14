ADEiger
===========
An
[EPICS](http://www.aps.anl.gov/epics/)
[areaDetector](https://github.com/areaDetector/areaDetector/blob/master/README.md)
driver for the Eiger and Eiger2 pixel array detectors from
[Dectris](http://www.dectris.com).

Additional information:
* [Documentation](https://areadetector.github.io/areaDetector/ADEiger/eiger.html)
* [Release notes](RELEASE.md)

Notes:
------

* Depends on the Eiger1 having the firmware 1.6.4 or newer.
* Depends on the Eiger2 having the firmware 2020.2 or newer.
* Depends on ADCore R3-5 and ADSupport R1-7 or newer.
* This has only been tested on Linux 64-bit machines.

Before compiling:
-----------------

* Set `ZMQ` variables inside `ADEiger/configure/CONFIG_SITE` to point to a ZeroMQ installation location if a system package
  is not installed (`libzmq3-dev` on Debian systems).
  - ZeroMQ 4 is required

HDF5 Plugins:
-------------

If you want HDF5 applications to decompress LZ4 or BSLZ4 compressed files produced by the detector,
you will need to have the bitshuffle and LZ4 decompression plugins available. These are built in ADSupport.

The environment variable `HDF5_PLUGIN_PATH` should be set to `[your_path]/ADSupport/lib/linux-x86_64`.

How to run:
-----------

* Under `ADEiger/iocs/eigerIOC/iocBoot/iocEiger1/` there is already a ready to use IOC for the Eiger1, and in iocEiger2/ for the Eiger2:
  - Change the IP address in `st.cmd`.
  - Run `./st.cmd`.
* There are medm, edm, caQtDM, CSS-Boy, and CSS-Phoebus screens under `areaDetector/ADEiger/eigerApp/op/`.

