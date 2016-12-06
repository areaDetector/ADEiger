ADEiger Releases
==================

The latest untagged master branch can be obtained at
https://github.com/brunoseivam/ADEiger.

Tagged source code can be obtained at
https://github.com/brunoseivam/ADEiger/releases.

Release Notes
=============

R2-1
----

Depends on Eiger firmware 1.6.2+

* Added all MX parameters (Chi, Kappa, Omega, Phi, Two Theta)
* Speeds up acquisition by avoiding resending the same energy setting if it hasn't changed.
* Fix a bug that caused the driver to fail to download huge images.
* Use monitor timeout instead of polling.
* Added the following parameters to the driver:
 - Omega (tracks the Omega angle for every frame)
 - Countrate Correction Count Cutoff (`detector/config/countrate_correction_count_cutoff`)
 - Sensor Thickness (`detector/config/sensor_thickness`)
 - Sensor Material (`detector/config/sensor_material`)
 - X Pixel Size (`detector/config/x_pixel_size`)
 - Y Pixel Size (`detector/config/y_pixel_size`)
 - Description (`detector/config/description`)
 - ROI Mode (`detector/config/roi_mode`)
 - Compression Algorithm (`detector/config/compression`)
 - Pixel Mask Applied (`detector/config/pixel_mask_applied`)

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
