ADEiger Releases
==================

The latest untagged master branch can be obtained at
https://github.com/areaDetector/ADEiger.

Tagged source code can be obtained at
https://github.com/areaDetector/ADEiger/releases.

Release Notes
=============
R3-0 (June 25, 2021)
----
* Added support for Eiger2 detectors.
  These use a different version of the Simplon API (1.8.0) from most Eiger1 detectors which use 1.6.0.
  However, some Eiger1 detectors do use the 1.8.0 Simplon API.
  The Eiger2 detectors also have new features such as 2 energy thresholds rather than just 1.
  * The driver automatically detects which version of the API is in use (1.8.0 or 1.6.0) and what Eiger
    model is being used (Eiger1 or Eiger2).
  * The startup script loads either the eiger1.template or eiger2.template database file.
    Both of these load eigerBase.template, which contains the records that are common to both models.
  * The iocBoot/iocEiger directory has been renamed to iocEiger1 and is configured for an Eiger1 detector.
  * The new iocBoot/iocEiger2 directory is configured for an Eiger2 detector.
  * The following records have been added for both Eiger models:
    - CountrateCorrApplied (enable/disable the countrate correction)
    - PixelMaskApplied (enable/disable the pixel mask)
    - BitDepthImage_RBV (Bit depth for images on the Stream interface)
    - Initialize (initializes the server)
    - AutoSummation (enable/disable autosummation)
  * The following records are present only on the Eiger1:
    - FWClear (erase all files on the server)
    - Link0_RBV, Link1_RBV, Link2_RBV, Link3_RBV (status of the detector links)
    - DCUBufferFree_RBV (number of free buffers on the server)
  * The following records are present only on the Eiger2:
    - CountingMode (sets the counting mode to "normal" or "retrigger")
    - TriggerStartDelay (sets the delay time after a trigger is received)
    - Threshold1Enable (enables the first threshold)
    - Threshold2Energy (energy of the second threshold)
    - Threshold2Enable (enables the second threshold)
    - ThresholdDiffEnable (enables calculation of difference image, threshold1-threshold2)
    - HVResetTime (set the time that the high voltage is off when HVReset is executed)
    - HVReset (resets the high voltage.  Meant for use with CdTe models)
    - HVState (the current state of the high voltage)
    - CompressionAlgo has an additional choice, "None", which sends uncompressed arrays over the Stream interface.
      The stream interface now supports uncompressed arrays.
  * The following records are supported in new Eiger2 firmware that will be released in early 2021, but
    is currently available for testing.  The records are present in ADBase.template or eiger2.template but
    a flag must be set in the driver to enable the logic for them.
    - TriggerMode has an additional option, ExternalGate.
    - ExtGateMode (selects "Pump & Probe" or "HDR" for high dynamic range)
    - NumExposures (selects the number of exposures per image)
  * Created new top-level medm screen, eiger2Detector.adl, and also the auto-converted OPI screens.
  * Created new eiger1_settings.req and eiger2_settings.req for autosave.
    These both load eigerBase_settings.req for records common to both models.
* Added support for frames with UInt8 datatype.  At very high frame rates the Eiger sends 8-bit data,
  but the driver was not handling this, and would crash.
* Fixed an issue with acquisition not stopping automatically when TriggerMode=External Series or External Enable.
  - Previously it was necessary to manually set Acquire=0 to stop acquisition in these modes.
  - Now acquisition will automatically stop when the value of NumImagesCounter_RBV equals the expected
    number of images to be collected, which is NumTriggers_RBV * NumImages_RBV.
    This mechanism has been tested in both External Series and External Enable mode, with DataSource=Stream or
    DataSource=FileWriter.  It will not work with DataSource=None because then there is nothing updating
    NumImagesCounter_RBV.  In that case manually setting Acquire=0 is still necessary.
* The PREC fields for a number of records with units of time were decreased, and the OPI screens were
  changed to use exponential notation, which is easier to read and modify.
* The enum choices for records that enable/disable a feature were not consistent.
  Some used Enable/Disable (which is the standard in ADCore), some used Enabled/Disabled, and some used Yes/No.
  Changed so all records now use Enable/Disable.  
  Set the disabled (0) state to have NO_ALARM and the enabled state to have MINOR alarm.
  OPI screens now use alarm color mode, which makes it easier to see the state at a glance.
  The numeric values associated with the logical states have not changed, 0=Disable, 1=Enable. 
  NOTE: This may break backwards compatibility with clients if they use the strings rather than
  the numeric value to set the state.
* Added additional records to the OPI screens that were already present in the template files:
  - FileOwner, FileOwnerGrp, FilePerms
  - DeadTime_RBV
  - StreamHdrDetail
* Fixed a problem that occurs with 2020.2.2 firmware on the Eiger2.  
  Changing the TriggerMode to Internal Enable ("inte") or External Enable ("exte") fails if NumImages is not 1.
  The driver was changed to set NumImages=1 just before changing TriggerMode to either of these values.
* Improved the layout of eigerDetector.adl, with new widgets and new sub-screens,
  some of which are common to the Eiger2 and some specific to the Eiger1.

R2-7 (December 22, 2020)
----
* Added support for decompressing bitshuffle/lz4 compressed files on the Stream interface.
  Previously it could only decompress lz4 without bitshuffle.
  This meant that if the Eiger server was saving bitshuffle/lz4 it was not possible to read
  the data into areaDetector, because neither the Stream or Filewriter interfaces supported 
  that compressor.
* Added StreamDecompress record. This controls whether the driver decompresses the arrays from the Stream interface.
  * If StreamDecompress=Yes (default), then the NDArrays received by plugins are decompressed. This was the previous behavior.
  * If StreamDecompress=No then the NDArrays received by plugins are compressed,
    with the .codec and .compressedSize fields set appropriately. This mode can be useful for passing
    compressed arrays directly to NDPluginPva (and then to ImageJ) and to NDFileHDF5 since that supports DirectChunkWrite.
  * Other plugins that do not support compressed arrays will need to get their data from an NDPluginCodec plugin
    that is configured to decompress the NDArrays from ADEiger.
    This configuration has the advantage that the decompression is offloaded from the driver, and hence can use 
    more cores and can queue NDArrays in case it cannot keep up.
* This version requires ADSupport R1-7 and ADCore R3-5 where bitshuffle support was added.
* Removed the lz4Src directory from ADEiger.  It now uses the lz4 functions from bitshuffleSrc in ADSupport.

R2-6 (December 5, 2018)
----
* Driver fixes and improvements
  * Avoid error messages when polling status on Eiger 500K, which does not have link2 and link3.
  * Avoid error message when polling status and acquisition is active.  
    The detector status is now not polled when acquisition is active.
  * Previously when DataSource was changed to Stream it was necessary to manually set StreamEnable to No and then Yes
    for it to work properly.  This is now done automatically in the driver.
  * Added support for NumImagesCounter_RBV which counts the number of images collected in the current acquisition.
* Many improvements to medm adl files
  * Added many missing PVs and included screens (e.g. ADSetup, ADBuffers, ADAttrFile)
  * Fixed formatting
  * Added Makefiles to autoconvert medm adl files to files in new ui/autoconvert, edl/autoconvert, and opi/autoconvert directories
* Uncommented commonPluginSettings.req in iocEiger/auto_settings.req so plugin settings will be saved and restored.
* Changed record types of integer parameters in eiger.template from ao/ai to longout/longin.

R2-5 (February 6, 2018)
----
* ADEiger moved to the areaDetector organization.

R2-4 (February 6, 2018)
----
* Added EDM screens, courtesy of Gary Yendell from Diamond Light Source
* Increase precision of double -> string conversion to maximum (thanks to Gary
Yendell)

R2-3-1 (September 6, 2018)
----
* Fix `envPaths` not being generated by `make all` at ADEiger root.
* Fix `pollTask` sometimes skipping files.
* Fix trigger mode ordering issue introduced in R2-2.
* Fix `streamTask` getting confused when receiving an "end" frame from a previous acquisition.

R2-3 (July 12, 2017)
----

* Prevent file from being deleted from detector's disk if it wasn't properly saved to local disk.
* Fix files not being saved when the driver was too busy.
* Detector's disk free space is now displayed in GB instead of kB
* Increase `SensorThickness_RBV.PREC` to 6 
* Fetch the following parameters when updating the status:
    * FileWriter Free Space
    * Stream API Dropped frames
* Added the following parameters to the driver:
    * Stream API header detail (`stream/config/header_detail`)
    * Stream API header appendix (`stream/config/header_appendix`)
    * Stream API image appendix (`stream/config/image_appendix`)

R2-2-2 (May 22, 2017)
------

* Fix Segmentation Fault when using the Monitor
* Fix error when clearing the files in the FileWriter
* Fix error when parsing `roi_mode` from Eigers that don't have this feature

R2-2-1 (April 27, 2017)
------

* Fix file owner/group not being set.


R2-2 (April 27, 2017)
----

Depends on Eiger firmware 1.6.4+, ADCore R2-6+

* Major rewrite of how the driver accesses detector parameters:
    * Value limits are enforced by the driver before being sent to the detector
* New parameters can be accessed just with a template file, see documentation
* Added MEDM screens, courtesy of Vesna Samardzic-Boban (Australian Synchrotron) and Zachary Brown (CHESS)
* Fix status not updating
* Fix incorrect SensorMaterial_RBV DTYP in database file
* Fix incorrect SensorMaterial_RBV display in OPI
* Prevent using FileWriter or Stream as a Data Source when the selected compression algorithm is Bit Shuffle LZ4.
* Added PV's to set owner, group and permissions on downloaded files.
    * This depends on the IOC executable having the appropriate `CAP_SETUID` and `CAP_SETGID` capabilities set.
* Extra header data in the streaming interface is now ignored if `header_detail` is other than "none". 
* Added the following parameters to the driver:
    * Dead Time (`detector/config/detector_readout_time`)
    * FileWriter State (`filewriter/status/state`)
    * Monitor State (`monitor/status/state`)
    * Stream State (`stream/status/state`)

R2-1 (Decemeber 6, 2016)
----

Depends on Eiger firmware 1.6.2+

* Added all MX parameters (Chi, Kappa, Omega, Phi, Two Theta)
* Speeds up acquisition by avoiding resending the same energy setting if it hasn't changed.
* Fix a bug that caused the driver to fail to download huge images.
* Use monitor timeout instead of polling.
* Added the following parameters to the driver:
    * Omega (tracks the Omega angle for every frame)
    * Countrate Correction Count Cutoff (`detector/config/countrate_correction_count_cutoff`)
    * Sensor Thickness (`detector/config/sensor_thickness`)
    * Sensor Material (`detector/config/sensor_material`)
    * X Pixel Size (`detector/config/x_pixel_size`)
    * Y Pixel Size (`detector/config/y_pixel_size`)
    * Description (`detector/config/description`)
    * ROI Mode (`detector/config/roi_mode`)
    * Compression Algorithm (`detector/config/compression`)
    * Pixel Mask Applied (`detector/config/pixel_mask_applied`)

R2-0 (April 27, 2016)
----

Depends on Eiger firmware 1.5.0+

* Added support for auto removing files on detector disk
* Added support for ZeroMQ streaming interface.
* Added support for Monitor interface.
* Added macro prefix to paths to other areaDetector screens on OPI.
* Added parameters to track FileWriter disk usage and DCU buffer usage.
* Several fixes and improvements.

R1-0 (August 13, 2015)
----
* Supports all trigger modes: INTS, INTE, EXTS and EXTE.
* Redesigned with a multithreaded architecture:
    * Downloading, saving to disk and parsing HDF5 files all occur in parallel.
* Faster file download.
* Eiger API and areaDetector driver decoupled.

R0-1-pre (April 15, 2015)
--------
* First release.
* Supports only INTS mode.
* Cannot interrupt long acquisitions.
