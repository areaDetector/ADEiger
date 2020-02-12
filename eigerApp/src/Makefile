TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

LIBRARY_IOC_Linux = eigerDetector
LIBRARY_IOC_Darwin = eigerDetector

USR_INCLUDES += -I$(TOP)/eigerApp/frozenSrc
USR_INCLUDES += $(addprefix -I, $(HDF5_INCLUDE))
USR_INCLUDES += $(addprefix -I, $(ZMQ_INCLUDE))
USR_INCLUDES += $(addprefix -I, $(LZ4_INCLUDE))

LIB_SRCS += eigerDetector.cpp
LIB_SRCS += restApi.cpp streamApi.cpp eigerParam.cpp

DBD += eigerDetectorSupport.dbd

ifdef LZ4_LIB
  lz4_DIR  += $(LZ4_LIB)
  LIB_LIBS += lz4
  bitshuffle_DIR += $(LZ4_LIB)
  LIB_LIBS       += bitshuffle
else
  LIB_SYS_LIBS  += lz4
  LIB_SYS_LIBS  += bitshuffle
endif

ifdef ZMQ_LIB
  zmq_DIR       += $(ZMQ_LIB)
  LIB_LIBS      += zmq
else
  LIB_SYS_LIBS  += zmq
endif

ifdef HDF5_LIB
  hdf5_hl_DIR   += $(HDF5_LIB)
  LIB_LIBS      += hdf5_hl
else
  LIB_SYS_LIBS  += hdf5_hl
endif

include $(ADCORE)/ADApp/commonLibraryMakefile

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
