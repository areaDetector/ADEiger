< envPaths
errlogInit(20000)

dbLoadDatabase("$(TOP)/dbd/eigerDetectorApp.dbd")
eigerDetectorApp_registerRecordDeviceDriver(pdbbase)

epicsEnvSet("PREFIX", "13EIG1:")
epicsEnvSet("PORT",   "EIG")
epicsEnvSet("QSIZE",  "20")
epicsEnvSet("XSIZE",  "1030")
epicsEnvSet("YSIZE",  "1065")
epicsEnvSet("NCHANS", "2048")
epicsEnvSet("CBUFFS", "500")
epicsEnvSet("EIGERIP", "130.199.221.165")
epicsEnvSet("EPICS_DB_INCLUDE_PATH", "$(ADCORE)/db")
epicsEnvSet("EPICS_CA_MAX_ARRAY_BYTES", "5000000")

eigerDetectorConfig("$(PORT)", "$(EIGERIP)", 0, 0)
dbLoadRecords("$(ADEIGER)/db/eiger.template", "P=$(PREFIX),R=cam1:,PORT=$(PORT),ADDR=0,TIMEOUT=1")

# Create a standard arrays plugin
NDStdArraysConfigure("Image1", 5, 0, "$(PORT)", 0, 0)
dbLoadRecords("$(ADCORE)/db/NDStdArrays.template", "P=$(PREFIX),R=image1:,PORT=Image1,ADDR=0,TIMEOUT=1,TYPE=Int32,FTVL=LONG,NELEMENTS=1096950, NDARRAY_PORT=$(PORT)")

NDStdArraysConfigure("Image2", 5, 0, "$(PORT)", 1, 0)
dbLoadRecords("$(ADCORE)/db/NDStdArrays.template", "P=$(PREFIX),R=image2:,PORT=Image2,ADDR=0,TIMEOUT=1,TYPE=Int32,FTVL=LONG,NELEMENTS=1096950, NDARRAY_PORT=$(PORT)")

# Load all other plugins using commonPlugins.cmd
< $(ADCORE)/iocBoot/commonPlugins.cmd
set_requestfile_path("$(ADEIGER)/eigerApp/Db")

#asynSetTraceMask("$(PORT)",0,255)
#asynSetTraceMask("$(PORT)",0,3)

iocInit()

# save things every thirty seconds
create_monitor_set("auto_settings.req", 30, "P=$(PREFIX)")
