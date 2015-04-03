< envPaths
errlogInit(20000)

dbLoadDatabase("$(TOP)/dbd/eigerDetectorApp.dbd")
eigerDetectorApp_registerRecordDeviceDriver(pdbbase)

epicsEnvSet("PREFIX", "13EIG1:")
epicsEnvSet("PORT",   "EIG")
epicsEnvSet("QSIZE",  "20")
epicsEnvSet("NCHANS", "2048")
epicsEnvSet("EIGERIP", "130.199.221.222")
epicsEnvSet("EPICS_DB_INCLUDE_PATH", "$(ADCORE)/db")

###
# Create the asyn port to talk to the eiger on port 80.
drvAsynIPPortConfigure("server","$(EIGERIP):80 HTTP")

eigerDetectorConfig("$(PORT)", "server", 0, 0)
dbLoadRecords("$(ADCORE)/db/ADBase.template", "P=$(PREFIX),R=cam1:,PORT=$(PORT),ADDR=0,TIMEOUT=1")
dbLoadRecords("$(ADCORE)/db/NDFile.template", "P=$(PREFIX),R=cam1:,PORT=$(PORT),ADDR=0,TIMEOUT=1")
dbLoadRecords("$(ADEIGER)/db/eiger.template","P=$(PREFIX),R=cam1:,PORT=$(PORT),ADDR=0,TIMEOUT=1,SERVER_PORT=server")

# Create a standard arrays plugin
NDStdArraysConfigure("Image1", 5, 0, "$(PORT)", 0, 0)
dbLoadRecords("$(ADCORE)/db/NDStdArrays.template", "P=$(PREFIX),R=image1:,PORT=Image1,ADDR=0,TIMEOUT=1,TYPE=Int32,FTVL=LONG,NELEMENTS=94965, NDARRAY_PORT=$(PORT)")

# Load all other plugins using commonPlugins.cmd
# < $(ADCORE)/iocBoot/commonPlugins.cmd
set_requestfile_path("$(ADEIGER)/eigerApp/Db")

#asynSetTraceMask("$(PORT)",0,255)
#asynSetTraceMask("$(PORT)",0,3)


iocInit()

# save things every thirty seconds
# create_monitor_set("auto_settings.req", 30,"P=$(PREFIX)")
