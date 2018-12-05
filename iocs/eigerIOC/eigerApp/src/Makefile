TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

PROD_NAME = eigerDetectorApp
PROD_IOC_Linux  += $(PROD_NAME)
PROD_IOC_Darwin += $(PROD_NAME)

# <name>.dbd will be created from <name>Include.dbd
DBD += $(PROD_NAME).dbd
$(PROD_NAME)_DBD += eigerDetectorSupport.dbd

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
PROD_SRCS += $(PROD_NAME)_registerRecordDeviceDriver.cpp $(PROD_NAME)Main.cpp

# Note that the lz4 library will be found in ADSupport via the blosc package
# So this application must be built with WITH_BLOSC=YES and BLOSC_EXTERNAL=NO.
PROD_LIBS += eigerDetector frozen hdf5_hl

ifdef ZMQ_LIB
  zmq_DIR        += $(ZMQ_LIB)
  PROD_LIBS      += zmq
else
  PROD_SYS_LIBS  += zmq
endif

include $(ADCORE)/ADApp/commonDriverMakefile

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

