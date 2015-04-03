TOP = ..
include $(TOP)/configure/CONFIG

DIRS := $(DIRS) frozenSrc
DIRS := $(DIRS) src
src_DEPEND_DIRS += frozenSrc
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *db*))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *Db*))

include $(TOP)/configure/RULES_DIRS

