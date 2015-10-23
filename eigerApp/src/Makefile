TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

LIBRARY_IOC = eigerDetector

USR_INCLUDES += -I$(TOP)/eigerApp/frozenSrc
USR_INCLUDES += -I$(TOP)/eigerApp/lz4Src
USR_INCLUDES += $(HDF5_INCLUDE)

LIB_SRCS += eigerDetector.cpp
LIB_SRCS += restApi.cpp

DBD += eigerDetectorSupport.dbd

LIB_LIBS_Linux += hdf5_hl 
hdf5_hl_DIR = $(HDF5_LIB)

include $(ADCORE)/ADApp/commonLibraryMakefile

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

