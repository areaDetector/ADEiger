TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

LIBRARY_IOC = eigerDetector

USR_INCLUDES += -I$(TOP)/eigerApp/frozenSrc
USR_INCLUDES += -I$(TOP)/eigerApp/lz4Src
USR_INCLUDES += $(HDF5_INCLUDE)
USR_INCLUDES += $(ZMQ_INCLUDE)

LIB_SRCS += eigerDetector.cpp
LIB_SRCS += restApi.cpp streamApi.cpp eigerParam.cpp

DBD += eigerDetectorSupport.dbd

ifdef ZMQ_LIB
  zmq_DIR       += $(ZMQ_LIB)
  LIB_LIBS      += zmq
else
  LIB_SYS_LIBS  += zmq
endif

ifdef HDF5_LIB
  hdf5_hl_DIR   += $(HDF5_LIB)
  LIB_LIBS      += hdf5_hl
endif

include $(ADCORE)/ADApp/commonLibraryMakefile

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

