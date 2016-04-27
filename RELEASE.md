ADEiger Releases
==================

The latest untagged master branch can be obtained at
https://github.com/brunoseivam/ADEiger.

Tagged source code can be obtained at
https://github.com/brunoseivam/ADEiger/releases.

Release Notes
=============

R2-0
----

Depends on Eiger firmware 1.5.0+

* Added support for auto removing files on detector disk
* Added support for ZeroMQ streaming interface.
* Added support for Monitor interface.
* Added macro prefix to paths to other areaDetector screens on OPI.
* Added parameters to track FileWriter disk usage and DCU buffer usage.
* Several fixes and improvements.

R1-0
----
* Supports all trigger modes: INTS, INTE, EXTS and EXTE.
* Redesigned with a multithreaded architecture:
 - Downloading, saving to disk and parsing HDF5 files all occur in parallel.
* Faster file download.
* Eiger API and areaDetector driver decoupled.

R0-1-pre
--------
* First release.
* Supports only INTS mode.
* Cannot interrupt long acquisitions.

Future Releases
===============
