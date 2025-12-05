TOP=../..
include $(TOP)/configure/CONFIG
#---------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=======================================

INC += rfc3339.h

TESTPROD_HOST += rfc3339Test
rfc3339Test_SRCS += rfc3339Test.cpp
testHarness_SRCS += rfc3339Test.cpp
TESTS += rfc3339Test

PROD_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
#---------------------------------------
#  ADD RULES AFTER THIS LINE
#=======================================
