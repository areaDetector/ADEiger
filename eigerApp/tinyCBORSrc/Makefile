TOP=../..
include $(TOP)/configure/CONFIG
#---------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=======================================

LIBRARY_IOC_Linux = tinyCBOR
LIBRARY_IOC_Darwin = tinyCBOR

INC += cbor.h
INC += tinycbor-version.h

tinyCBOR_SRCS += 	cborerrorstrings.c
tinyCBOR_SRCS += 	cborencoder.c
tinyCBOR_SRCS += 	cborencoder_close_container_checked.c
tinyCBOR_SRCS += 	cborparser.c
tinyCBOR_SRCS += 	cborpretty.c
tinyCBOR_SRCS += 	cborparser_dup_string.c


include $(TOP)/configure/RULES
#---------------------------------------
#  ADD RULES AFTER THIS LINE
#=======================================
