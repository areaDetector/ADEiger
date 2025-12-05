TOP = ..
include $(TOP)/configure/CONFIG

DIRS := $(DIRS) frozenSrc
DIRS := $(DIRS) tinyCBORSrc
DIRS := $(DIRS) compressionSrc
DIRS := $(DIRS) rfc3339Src
DIRS := $(DIRS) src
src_DEPEND_DIRS += frozenSrc
src_DEPEND_DIRS += tinyCBORSrc
src_DEPEND_DIRS += compressionSrc
src_DEPEND_DIRS += rfc3339Src
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *db*))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *Db*))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard op*))

include $(TOP)/configure/RULES_DIRS
