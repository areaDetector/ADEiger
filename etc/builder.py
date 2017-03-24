from iocbuilder import AutoSubstitution
from iocbuilder.arginfo import makeArgInfo, Simple
from iocbuilder.modules.asyn import Asyn, AsynPort, AsynIP
from iocbuilder.modules.ADCore import ADCore, ADBaseTemplate, makeTemplateInstance, includesTemplates, NDDataTypes


__all__ = ['eigerDetector']


class eigerDetectorTemplate(AutoSubstitution):
    TemplateFile = "eiger.template"


class eigerDetector(AsynPort):
    """Create an eiger detector"""
    Dependencies = (ADCore,)

    # This tells xmlbuilder to use PORT instead of name as the row ID
    UniqueName = "PORT"

    _SpecificTemplate = eigerDetectorTemplate

    def __init__(self, PORT, SERVER, BUFFERS = 50, MEMORY = 0, **args):
        # Init the superclass (AsynPort)
        self.__super.__init__(PORT)
        # Update the attributes of self from the commandline args
        self.__dict__.update(locals())
        # Make an instance of our template
        makeTemplateInstance(self._SpecificTemplate, locals(), args)

    # __init__ arguments
    ArgInfo = ADBaseTemplate.ArgInfo + _SpecificTemplate.ArgInfo + makeArgInfo(__init__,
        PORT=Simple('Port name for the detector', str),
        SERVER=Simple('Server host name', str),
        BUFFERS=Simple('Maximum number of NDArray buffers to be created for plugin callbacks', int),
        MEMORY=Simple('Max memory to allocate, should be maxw*maxh*nbuffer for driver and all attached plugins', int))

    # Device attributes
    LibFileList = ['eigerDetector', 'frozen', 'lz4']
    DbdFileList = ['eigerDetectorSupport']

    def Initialise(self):
        print "# eigerDetectorConfig(const char *portName, const char *serverPort, int maxBuffers, size_t maxMemory, int priority, int stackSize)"
        print 'eigerDetectorConfig("%(PORT)s", %(SERVER)s, %(BUFFERS)s, %(MEMORY)d)' % self.__dict__
