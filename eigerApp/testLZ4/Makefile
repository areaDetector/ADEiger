TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

PROD += test_lz4
test_lz4_SRCS += test_lz4.cpp
test_lz4_LIBS += dectrisCompression
test_lz4_SYS_LIBS += lz4

include $(ADCORE)/ADApp/commonLibraryMakefile

#=============================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

