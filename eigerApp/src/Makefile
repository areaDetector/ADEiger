TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

LIBRARY_IOC_Linux = eigerDetector
LIBRARY_IOC_Darwin = eigerDetector

USR_INCLUDES += -I$(TOP)/eigerApp/frozenSrc

LIB_SRCS += eigerDetector.cpp

DBD += eigerDetectorSupport.dbd

LIB_LIBS_Linux += hdf5_hl 
hdf5_hl_DIR = $(HDF5_LIB)
USR_INCLUDES += $(HDF5_INCLUDE)

include $(ADCORE)/ADApp/commonLibraryMakefile

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

