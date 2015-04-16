ADEiger
===========
An 
[EPICS](http://www.aps.anl.gov/epics/) 
[areaDetector](http://cars.uchicago.edu/software/epics/areaDetector.html) 
driver for the Eiger pixel array detectors from 
[Dectris](http://www.dectris.com).

Additional information:
* [Documentation](http://cars.uchicago.edu/software/epics/eigerDoc.html).
* [Release notes and links to source and binary releases](RELEASE.md).

How to install:
---------------

* Clone and compile [areaDetector](https://github.com/areaDetector/areaDetector) according to its instructions. 
* Clone this repository inside the `areaDetector` folder.
* Run `make` inside `ADEiger`.
* Under `areaDetector/ADEiger/iocs/eigerIOC/iocBoot/iocEiger/` there is already a ready to use IOC:
  - If there is no `envPaths`, run `make envPaths` there.
  - If there is no `envPaths.linux`, run `ln -s envPaths envPaths.linux` there.
  - Fix the IP address in `st.cmd`.
  - Run `./start_epics`.
* There is a CSS screen under `areaDetector/ADEiger/eigerApp/op/opi`, just open it with Controls System Studio.

