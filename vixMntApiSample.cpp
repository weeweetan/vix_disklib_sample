/*
 * vixMntApiSample.cpp --
 *
 *      Sample program to demonstrate mounting of vmdk on proxy using 
 *      vixDiskLib and vixMntApi DLL.
 */

#ifdef _WIN32
#include <windows.h>
#include <tchar.h>
#include <process.h>
#else
#include <dlfcn.h>
#include <sys/time.h>
#endif

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <string.h>
#include <vector>
#include <stdexcept>
#include <memory>
#include <assert.h>
#include "vixDiskLib.h"
#include "vixMntApi.h"

using std::cout;
using std::cin;
using std::string;
using std::endl;
using std::vector;

#define COMMAND_INFO            (1 << 0)
#define COMMAND_DUMP_META       (1 << 1)
#define COMMAND_READ_META       (1 << 2)
#define COMMAND_WRITE_META      (1 << 3)
#define COMMAND_MOUNT           (1 << 4)

#define VIXDISKLIB_VERSION_MAJOR 5
#define VIXDISKLIB_VERSION_MINOR 5

#define ERROR_MNTAPI_VOLUME_ALREADY_MOUNTED			 24305

static struct {
    int command;
    char *transportModes;
    char *diskPath;
	char *mntDiskPaths[20];
    char *metaKey;
    char *metaVal;
    uint32 openFlags;
    Bool isRemote;
    char *host;
    char *userName;
    char *password;
    char *thumbPrint;
    int port;
    int nfcHostPort;
    char *srcPath;
    VixDiskLibConnection connection;
    char *vmxSpec;
    bool useInitEx;
    char *cfgFile;
    char *libdir;
    char *ssMoRef;
} appGlobals;

static int ParseArguments(int argc, char* argv[]);
static void DoReadMetadata(void);
static void DoWriteMetadata(void);
static void DoDumpMetadata(void);
static void DoInfo(void);
static void DoMount(const vector<string> &disks);
static int BitCount(int number);

#define THROW_ERROR(vixError) \
   throw VixDiskLibErrWrapper((vixError), __FILE__, __LINE__)

#define CHECK_AND_THROW(vixError)                                    \
   do {                                                              \
      if (VIX_FAILED((vixError))) {                                  \
         throw VixDiskLibErrWrapper((vixError), __FILE__, __LINE__); \
      }                                                              \
   } while (0)


/*
 *--------------------------------------------------------------------------
 *
 * LogFunc --
 *
 *      Callback for VixDiskLib Log messages.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

static void
LogFunc(const char *fmt, va_list args)
{
   printf("Log: ");
   vprintf(fmt, args);
}


/*
 *--------------------------------------------------------------------------
 *
 * WarnFunc --
 *
 *      Callback for VixDiskLib Warning messages.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

static void
WarnFunc(const char *fmt, va_list args)
{
   printf("Warning: ");
   vprintf(fmt, args);
}


/*
 *--------------------------------------------------------------------------
 *
 * PanicFunc --
 *
 *      Callback for VixDiskLib Panic messages.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

static void
PanicFunc(const char *fmt, va_list args)
{
   printf("Panic: ");
   vprintf(fmt, args);
   exit(10);
}

#define CHECK(vixError__, label__)   \
do  {\
   char *errorText = NULL;                                                     \
   if (VIX_FAILED(vixError__)) {                                                \
       printf("Failed at %s:%d, errorcode : %s (%"FMT64"d)\n",                      \
              __FILE__, __LINE__, VixDiskLib_GetErrorText(vixError__, NULL), vixError__);\
       VixDiskLib_FreeErrorText(errorText);                                         \
       goto label__;                                                                \
   }                                                                            \
} while (0)

typedef struct {
   VixVolumeHandle volumeHandle;
   VixVolumeInfo* volInfo;
} MountedVolume;

vector<MountedVolume> mountedVolumes;
vector<VixDiskSetHandle> mountedDisks;

typedef void (VixDiskLibGenericLogFunc)(const char *fmt, va_list args);


// Wrapper class for VixDiskLib disk objects.

class VixDiskLibErrWrapper
{
public:
    explicit VixDiskLibErrWrapper(VixError errCode, const char* file, int line)
          :
          _errCode(errCode),
          _file(file),
          _line(line)
    {
        char* msg = VixDiskLib_GetErrorText(errCode, NULL);
        _desc = msg;
        VixDiskLib_FreeErrorText(msg);
    }

    VixDiskLibErrWrapper(const char* description, const char* file, int line)
          :
         _errCode(VIX_E_FAIL),
         _desc(description),
         _file(file),
         _line(line)
    {
    }

    string Description() const { return _desc; }
    VixError ErrorCode() const { return _errCode; }
    string File() const { return _file; }
    int Line() const { return _line; }

private:
    VixError _errCode;
    string _desc;
    string _file;
    int _line;
};

class VixDisk
{
public:

    VixDiskLibHandle Handle() { return _handle; }
    VixDisk(VixDiskLibConnection connection, char *path, uint32 flags)
    {
       _handle = NULL;
       VixError vixError = VixDiskLib_Open(connection, path, flags, &_handle);
       CHECK_AND_THROW(vixError);
       printf("Disk \"%s\" is open using transport mode \"%s\".\n",
              path, VixDiskLib_GetTransportMode(_handle));
    }

    ~VixDisk()
    {
        if (_handle) {
           VixDiskLib_Close(_handle);
        }
        _handle = NULL;
    }

private:
    VixDiskLibHandle _handle;
};


/*
 *--------------------------------------------------------------------------
 *
 * PrintUsage --
 *
 *      Displays the usage message.
 *
 * Results:
 *      1.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

static int
PrintUsage(void)
{
    printf("Usage: vixMntApiSample.exe command [options] diskPath\n\n");

    printf("List of commands (all commands are mutually exclusive):\n");
    printf(" -info : displays information for specified virtual disk\n");
    printf(" -wmeta key value : writes (key,value) entry into disk's metadata table\n");
    printf(" -rmeta key : displays the value of the specified metada entry\n");
    printf(" -meta : dumps all entries of the disk's metadata\n");
	printf(" -mount : Mounts target VM disk on to the proxy.\n\n");

    printf("options:\n");
    printf(" -host hostname : hostname/IP address of VC/vSphere host (Mandatory)\n");
    printf(" -user userid : user name on host (Mandatory) \n");
    printf(" -password password : password on host. (Mandatory)\n");
    printf(" -port port : port to use to connect to VC/ESXi host (default = 443) \n");
    printf(" -nfchostport port : port to use to establish NFC connection to ESXi host (default = 902) \n");
    printf(" -vm moref=id : id is the managed object reference of the VM \n");
    printf(" -libdir dir : Folder location of the VDDK installation. "
           "On Windows, the bin folder holds the plugin.  On Linux, it is "
           "the lib64 directory\n");
    printf(" -initex configfile : Specify path and filename of config file \n");
    printf(" -ssmoref moref : Managed object reference of VM snapshot \n");
    printf(" -mode mode : Mode string to pass into VixDiskLib_ConnectEx. "
	        "Valid modes are: nbd, nbdssl, san, hotadd \n");
    printf(" -thumb string : Provides a SSL thumbprint string for validation. "
           "Format: xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx:xx\n");
    
    return 1;
}


/*
 *--------------------------------------------------------------------------
 *
 * main --
 *
 *      Main routine of the program.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

int
main(int argc, char* argv[])
{
    int retval;
    bool bVixInit(false);
	vector<string> disks;
	    
    memset(&appGlobals, 0, sizeof appGlobals);
    appGlobals.command = 0;
    appGlobals.openFlags = 0;
    appGlobals.isRemote = FALSE;

    retval = ParseArguments(argc, argv);
    if (retval) {
        return retval;
    }

    VixDiskLibConnectParams cnxParams = {0};
    if (appGlobals.isRemote) {
          cnxParams.vmxSpec = appGlobals.vmxSpec;
          cnxParams.serverName = appGlobals.host;
          cnxParams.credType = VIXDISKLIB_CRED_UID;
          cnxParams.creds.uid.userName = appGlobals.userName;
          cnxParams.creds.uid.password = appGlobals.password;
          cnxParams.thumbPrint = appGlobals.thumbPrint;
          cnxParams.port = appGlobals.port;
    }

	VixError vixError;  
    try {  
       if (appGlobals.useInitEx) {
          vixError = VixDiskLib_InitEx(VIXDISKLIB_VERSION_MAJOR,
                                       VIXDISKLIB_VERSION_MINOR,
                                       &LogFunc, &WarnFunc, &PanicFunc,
                                       appGlobals.libdir,
                                       appGlobals.cfgFile);
       } else {
          vixError = VixDiskLib_Init(VIXDISKLIB_VERSION_MAJOR,
                                     VIXDISKLIB_VERSION_MINOR,
                                     NULL, NULL, NULL, // Log, warn, panic
                                     appGlobals.libdir);
       }
       CHECK_AND_THROW(vixError);
       bVixInit = true;

       if (appGlobals.vmxSpec != NULL) {
          vixError = VixDiskLib_PrepareForAccess(&cnxParams, "Sample");
       }
       if (appGlobals.ssMoRef == NULL && appGlobals.transportModes == NULL) {
          vixError = VixDiskLib_Connect(&cnxParams,
                                        &appGlobals.connection);
       } else {
          Bool ro = (appGlobals.openFlags & VIXDISKLIB_FLAG_OPEN_READ_ONLY);
          vixError = VixDiskLib_ConnectEx(&cnxParams, ro, appGlobals.ssMoRef,
                                          appGlobals.transportModes,
                                          &appGlobals.connection);
       }
       CHECK_AND_THROW(vixError);
        if (appGlobals.command & COMMAND_INFO) {
            DoInfo();
        } else if (appGlobals.command & COMMAND_READ_META) {
            DoReadMetadata();
        } else if (appGlobals.command & COMMAND_WRITE_META) {
            DoWriteMetadata();
        } else if (appGlobals.command & COMMAND_DUMP_META) {
            DoDumpMetadata();
		} else if (appGlobals.command & COMMAND_MOUNT) {
			char again;
	        string disk;
			disks.push_back(appGlobals.diskPath);
			cout << "\n Disk - " << appGlobals.diskPath << " is entered for mounting, Would you like to enter multiple other disk paths (y/n)? ";
			cin >> again;
			cin.ignore(1, '\n');
			while (toupper(again) == 'Y') {
				cout << "\n Enter disk path: " << endl;
				getline(cin, disk);
				disks.push_back(disk);
				cout << "\n Would you like to enter another disk path (y/n)? ";
		        cin >> again;
		        cin.ignore(1, '\n');
			} 
	        DoMount(disks);
        }
        retval = 0;
    } catch (const VixDiskLibErrWrapper& e) {
       cout << "Error: [" << e.File() << ":" << e.Line() << "]  " <<
               std::hex << e.ErrorCode() << " " << e.Description() << "\n";
       retval = 1;
    }

    if (appGlobals.vmxSpec != NULL) {
       vixError = VixDiskLib_EndAccess(&cnxParams, "Sample");
    }
    if (appGlobals.connection != NULL) {
       VixDiskLib_Disconnect(appGlobals.connection);
    }
    if (bVixInit) {
       VixDiskLib_Exit();
    }
    return retval;
}


/*
 *--------------------------------------------------------------------------
 *
 * BitCount --
 *
 *      Counts all the bits set in an int.
 *
 * Results:
 *      Number of bits set to 1.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

static int
BitCount(int number)    // IN
{
    int bits = 0;
    while (number) {
        number = number & (number - 1);
        bits++;
    }
    return bits;
}

/*
 *--------------------------------------------------------------------------
 *
 * ParseArguments --
 *
 *      Parses the arguments passed on the command line.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

static int
ParseArguments(int argc, char* argv[])
{
    int i;
    if (argc < 3) {
        printf("Error: Too few arguments. See usage below.\n\n");
        return PrintUsage();
    }
    for (i = 1; i < argc - 1; i++) {
        if (!strcmp(argv[i], "-info")) {
            appGlobals.command |= COMMAND_INFO;
            appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY;
		} else if (!strcmp(argv[i], "-mount")) {
            appGlobals.command |= COMMAND_MOUNT;
            appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY; 
        } else if (!strcmp(argv[i], "-meta")) {
            appGlobals.command |= COMMAND_DUMP_META;
            appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY;
        } else if (!strcmp(argv[i], "-rmeta")) {
            appGlobals.command |= COMMAND_READ_META;
            if (i >= argc - 2) {
                printf("Error: The -rmeta command requires a key value to "
                       "be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.metaKey = argv[++i];
            appGlobals.openFlags |= VIXDISKLIB_FLAG_OPEN_READ_ONLY;
        } else if (!strcmp(argv[i], "-wmeta")) {
            appGlobals.command |= COMMAND_WRITE_META;
            if (i >= argc - 3) {
                printf("Error: The -wmeta command requires key and value to "
                       "be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.metaKey = argv[++i];
            appGlobals.metaVal = argv[++i];
        } else if (!strcmp(argv[i], "-host")) {
            if (i >= argc - 2) {
                printf("Error: The -host option requires the IP address "
                       "or name of the host to be specified. "
                       "See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.host = argv[++i];
            appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-user")) {
            if (i >= argc - 2) {
                printf("Error: The -user option requires a username "
                       "to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.userName = argv[++i];
            appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-password")) {
            if (i >= argc - 2) {
                printf("Error: The -password option requires a password "
                       "to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.password = argv[++i];
            appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-thumb")) {
            if (i >= argc - 2) {
                printf("Error: The -thumb option requires an SSL thumbprint "
                       "to be specified. See usage below.\n\n");
               return PrintUsage();
            }
            appGlobals.thumbPrint = argv[++i];
            appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-port")) {
            if (i >= argc - 2) {
                printf("Error: The -port option requires the host's port "
                       "number to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.port = strtol(argv[++i], NULL, 0);
            appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-nfchostport")) {
           if (i >= argc - 2) {
              return PrintUsage();
           }
           appGlobals.nfcHostPort = strtol(argv[++i], NULL, 0);
           appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-vm")) {
            if (i >= argc - 2) {
                printf("Error: The -vm option requires the moref id of "
                       "the vm to be specified. See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.vmxSpec = argv[++i];
            appGlobals.isRemote = TRUE;
        } else if (!strcmp(argv[i], "-libdir")) {
           if (i >= argc - 2) {
              printf("Error: The -libdir option requires the folder location "
                     "of the VDDK installation to be specified. "
                     "See usage below.\n\n");
              return PrintUsage();
           }
           appGlobals.libdir = argv[++i];
        } else if (!strcmp(argv[i], "-initex")) {
           if (i >= argc - 2) {
              printf("Error: The -initex option requires the path and filename "
                     "of the VDDK config file to be specified. "
                     "See usage below.\n\n");
              return PrintUsage();
           }
           appGlobals.useInitEx = true;
           appGlobals.cfgFile = argv[++i];
           if (appGlobals.cfgFile[0] == '\0') {
              appGlobals.cfgFile = NULL;
           }
        } else if (!strcmp(argv[i], "-ssmoref")) {
           if (i >= argc - 2) {
              printf("Error: The -ssmoref option requires the moref id "
                       "of a VM snapshot to be specified. "
                       "See usage below.\n\n");
              return PrintUsage();
           }
           appGlobals.ssMoRef = argv[++i];
        } else if (!strcmp(argv[i], "-mode")) {
            if (i >= argc - 2) {
                printf("Error: The -mode option requires a mode string to  "
                       "connect to VixDiskLib_ConnectEx. Valid modes are "
                        "'nbd', 'nbdssl', 'san' and 'hotadd'. "
                        "See usage below.\n\n");
                return PrintUsage();
            }
            appGlobals.transportModes = argv[++i];
        } else {
           printf("Error: Unknown command or option: %s\n", argv[i]);
           return PrintUsage();
        }
    }
    appGlobals.diskPath = argv[i];

    if (BitCount(appGlobals.command) != 1) {
       printf("Error: Missing command. See usage below.\n");
       return PrintUsage();
    }

    if (appGlobals.isRemote) {
       if (appGlobals.host == NULL ||
           appGlobals.userName == NULL ||
           appGlobals.password == NULL) {
           printf("Error: Missing a mandatory option. ");
           printf("-host, -user and -password must be specified. ");
           printf("See usage below.\n");
           return PrintUsage();
       }
    }

    return 0;
}


/*
 *--------------------------------------------------------------------------
 *
 * DoInfo --
 *
 *      Queries the information of a virtual disk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

static void
DoInfo(void)
{
    VixDisk disk(appGlobals.connection, appGlobals.diskPath, appGlobals.openFlags);
    VixDiskLibInfo *info = NULL;
    VixError vixError;

    vixError = VixDiskLib_GetInfo(disk.Handle(), &info);

    CHECK_AND_THROW(vixError);

    cout << "capacity          = " << info->capacity << " sectors" << endl;
    cout << "number of links   = " << info->numLinks << endl;
    cout << "adapter type      = ";
    switch (info->adapterType) {
    case VIXDISKLIB_ADAPTER_IDE:
       cout << "IDE" << endl;
       break;
    case VIXDISKLIB_ADAPTER_SCSI_BUSLOGIC:
       cout << "BusLogic SCSI" << endl;
       break;
    case VIXDISKLIB_ADAPTER_SCSI_LSILOGIC:
       cout << "LsiLogic SCSI" << endl;
       break;
    default:
       cout << "unknown" << endl;
       break;
    }

    cout << "BIOS geometry     = " << info->biosGeo.cylinders <<
       "/" << info->biosGeo.heads << "/" << info->biosGeo.sectors << endl;

    cout << "physical geometry = " << info->physGeo.cylinders <<
       "/" << info->physGeo.heads << "/" << info->physGeo.sectors << endl;

    VixDiskLib_FreeInfo(info);

    cout << "Transport modes supported by vixDiskLib: " <<
       VixDiskLib_ListTransportModes() << endl;
}

/*
 *--------------------------------------------------------------------------
 *
 * DoReadMetadata --
 *
 *      Reads metadata from a virtual disk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

static void
DoReadMetadata(void)
{
    size_t requiredLen;
    VixDisk disk(appGlobals.connection, appGlobals.diskPath, appGlobals.openFlags);
    VixError vixError = VixDiskLib_ReadMetadata(disk.Handle(),
                                                appGlobals.metaKey,
                                                NULL, 0, &requiredLen);
    if (vixError != VIX_OK && vixError != VIX_E_BUFFER_TOOSMALL) {
        THROW_ERROR(vixError);
    }
    std::vector <char> val(requiredLen);
    vixError = VixDiskLib_ReadMetadata(disk.Handle(),
                                       appGlobals.metaKey,
                                       &val[0],
                                       requiredLen,
                                       NULL);
    CHECK_AND_THROW(vixError);
    cout << appGlobals.metaKey << " = " << &val[0] << endl;
}


/*
 *--------------------------------------------------------------------------
 *
 * DoWriteMetadata --
 *
 *      Writes metadata in a virtual disk.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

static void
DoWriteMetadata(void)
{
    VixDisk disk(appGlobals.connection, appGlobals.diskPath, appGlobals.openFlags);
    VixError vixError = VixDiskLib_WriteMetadata(disk.Handle(),
                                                 appGlobals.metaKey,
                                                 appGlobals.metaVal);
    CHECK_AND_THROW(vixError);
}


/*
 *--------------------------------------------------------------------------
 *
 * DoDumpMetadata --
 *
 *      Dumps all the metadata.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------------------
 */

static void
DoDumpMetadata(void)
{
    VixDisk disk(appGlobals.connection, appGlobals.diskPath, appGlobals.openFlags);
    char *key;
    size_t requiredLen;

    VixError vixError = VixDiskLib_GetMetadataKeys(disk.Handle(),
                                                   NULL, 0, &requiredLen);
    if (vixError != VIX_OK && vixError != VIX_E_BUFFER_TOOSMALL) {
       THROW_ERROR(vixError);
    }
    std::vector<char> buf(requiredLen);
    vixError = VixDiskLib_GetMetadataKeys(disk.Handle(), &buf[0], requiredLen, NULL);
    CHECK_AND_THROW(vixError);
    key = &buf[0];

    while (*key) {
        vixError = VixDiskLib_ReadMetadata(disk.Handle(), key, NULL, 0,
                                           &requiredLen);
        if (vixError != VIX_OK && vixError != VIX_E_BUFFER_TOOSMALL) {
           THROW_ERROR(vixError);
        }
        std::vector <char> val(requiredLen);
        vixError = VixDiskLib_ReadMetadata(disk.Handle(), key, &val[0],
                                           requiredLen, NULL);
        CHECK_AND_THROW(vixError);
        cout << key << " = " << &val[0] << endl;
        key += (1 + strlen(key));
    }
}


static void
UnmountDisks()
{
   vector<VixDiskSetHandle>::const_iterator iter = mountedDisks.begin();
   for (; iter != mountedDisks.end(); ++iter) {
      VixMntapi_CloseDiskSet(*iter);
   }
}

static void
UnmountVolumes()
{
   vector<MountedVolume>::const_iterator iter = mountedVolumes.begin();
   for (; iter != mountedVolumes.end(); ++iter) {
      VixMntapi_FreeVolumeInfo((*iter).volInfo);
      VixMntapi_DismountVolume((*iter).volumeHandle, TRUE);
   }
}

static void
DoMount(const vector<string> &disks)
{
   VixError vixError;
   VixDiskLibHandle *diskHandles;
   VixDiskLibHandle diskHandle, childHandle;
   VixDiskSetHandle diskSetHandle = NULL;
   VixVolumeHandle *volumeHandles = NULL;
   VixVolumeInfo *volInfo = NULL;
   VixDiskSetInfo *diskSetInfo = NULL;
   size_t numVolumes = 0;
   uint32 openFlags = VIXDISKLIB_FLAG_OPEN_READ_ONLY;   
   int j;
   vector<string> childDisks;
   
   int diskHandlesCount = disks.size();
   diskHandles = new VixDiskLibHandle[diskHandlesCount];
   for(int i=0; i < diskHandlesCount; i++) {
	   diskHandles[i] = NULL;
   }

   char *pch, *dup, buffer[50];
   dup = strdup(appGlobals.vmxSpec);
   pch = strtok(dup, "=");
   while (pch != NULL)
   {
    sprintf(buffer, "%s", pch);
    pch = strtok (NULL, "=");
   }
   for(int i=0; i<disks.size(); i++) {	 
	   std::stringstream childDiskName;
	   childDiskName << "C:\\" << (string)buffer << "-childDisk-" << (i+1) << ".vmdk";
	   childDisks.push_back(childDiskName.str());	   
   }
   free(dup);

   printf("Calling VixMntapi_Init...\n");
   vixError = VixMntapi_Init(VIXMNTAPI_MAJOR_VERSION, VIXMNTAPI_MINOR_VERSION,
                             &LogFunc, &WarnFunc, PanicFunc, 
							 appGlobals.libdir, appGlobals.cfgFile);
   CHECK(vixError, cleanup);

   // create local connection
   VixDiskLibConnection localConnection = NULL;
   vixError = VixDiskLib_Connect(NULL, &localConnection);
   CHECK(vixError, cleanup);

   vector<string>::const_iterator iter = disks.begin();
   for ( int i=0 ; iter != disks.end(); ++i, ++iter)
   {
      vixError = VixDiskLib_Open(appGlobals.connection,
                (*iter).c_str(),
                openFlags,
                &diskHandle);
      CHECK(vixError, cleanup);

      printf("Selected transport method: %s\n", VixDiskLib_GetTransportMode(diskHandle));

	  vixError = VixDiskLib_CreateChild(diskHandle, childDisks[i].c_str(), VIXDISKLIB_DISK_MONOLITHIC_SPARSE, NULL, NULL);
	  CHECK(vixError, cleanup);

	  vixError = VixDiskLib_Open(localConnection, childDisks[i].c_str(), VIXDISKLIB_FLAG_OPEN_SINGLE_LINK, &childHandle);
      CHECK(vixError, cleanup);
	  vixError = VixDiskLib_Attach(diskHandle, childHandle);
	  CHECK(vixError, cleanup);

      diskHandles[i] = childHandle;
   }

   printf("\nCalling VixMntapi_OpenDiskSet...\n");
   vixError = VixMntapi_OpenDiskSet(diskHandles,
                      disks.size(),
                      openFlags,
                      &diskSetHandle);
   CHECK(vixError, cleanup);

   mountedDisks.push_back(diskSetHandle);

   printf("\n\nCalling VixMntapi_GetDiskSetInfo...\n");
   vixError = VixMntapi_GetDiskSetInfo(diskSetHandle, &diskSetInfo);
   CHECK(vixError, cleanup);
   printf("DiskSet Info - flags %u (passed - %u), mountPoint %s.\n",
          diskSetInfo->openFlags, openFlags,
          diskSetInfo->mountPath);

   printf("\n\nCalling VixMntapi_GetVolumeHandles...\n");
   vixError = VixMntapi_GetVolumeHandles(diskSetHandle,
                                         &numVolumes,
                                         &volumeHandles);
   CHECK(vixError, cleanup);
   printf("\n\nNum Volumes %d\n", numVolumes);

   printf("Enter the volume number from which to start mounting...\n");
   scanf("%d", &j);
   
   volInfo = NULL;
   for (int i = j-1; i < numVolumes; ++i) {
      printf("\n\nMounting volume using VixMntapi_MountVolume...\n");
      vixError = VixMntapi_MountVolume(volumeHandles[i], TRUE);
	  if (vixError == ERROR_MNTAPI_VOLUME_ALREADY_MOUNTED) 
		 ; 
	  else
         CHECK(vixError, cleanup);
   }

   MountedVolume newVolume = {0, 0};
   for (int i = j-1; i < numVolumes; ++i) {
	  printf("\n\nGetting volume info using VixMntapi_GetVolumeInfo...\n");
      vixError = VixMntapi_GetVolumeInfo(volumeHandles[i], &newVolume.volInfo);
	  CHECK(vixError, cleanup);

      printf("\nMounted Volume %d, Type %d, isMounted %d, symLink %s, numGuestMountPoints %d (%s)\n\n",
             i, newVolume.volInfo->type, newVolume.volInfo->isMounted,
             newVolume.volInfo->symbolicLink == NULL ? "<null>" : newVolume.volInfo->symbolicLink,
             newVolume.volInfo->numGuestMountPoints,
             (newVolume.volInfo->numGuestMountPoints == 1) ? (newVolume.volInfo->inGuestMountPoints[0]) : "<null>" );
	  mountedVolumes.push_back(newVolume);
   
	  assert(volumeHandles[i]);
      assert(newVolume.volInfo);
      newVolume.volumeHandle = volumeHandles[i];
      std::wstringstream ansiSS;
      ansiSS << newVolume.volInfo->symbolicLink;
	  std::wstring sVolumeName = ansiSS.str();
      sVolumeName = sVolumeName.substr(3);
      sVolumeName.erase(sVolumeName.length()-1);
	  sVolumeName = L"\\Device" + sVolumeName;
	  cout << endl << endl << "Defining MS-DOS device name \"T:\" for volume " << newVolume.volInfo->symbolicLink << endl;
	  if ( DefineDosDeviceW( DDD_RAW_TARGET_PATH, L"T:", sVolumeName.c_str()) ) {
         HANDLE hDevice;
		 std::wstring wsVolume = L"\\\\.\\T:";
         hDevice = CreateFileW( wsVolume.c_str(),
                                GENERIC_READ,
                                0,
                                NULL,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                NULL );
			  
		 if ( hDevice == INVALID_HANDLE_VALUE ) {
            printf("Error opening volume, err = %d\n", GetLastError());
		 } else {
            WIN32_FIND_DATAW fdFile;
            std::wstring wsPath = L"T:\\*.*";
		    HANDLE hFind = FindFirstFileW(wsPath.c_str(), &fdFile);
			std::string MountPoint = (newVolume.volInfo->numGuestMountPoints == 1) ? (newVolume.volInfo->inGuestMountPoints[0]) : "<null>";
			cout << "=====================================================================================" << endl;
			cout << "=== Dumping contents of target VM's (" << MountPoint << ") drive (Mounted at T: drive on proxy) ===" << endl;
            cout << "=====================================================================================" << endl;
			while ( hFind != INVALID_HANDLE_VALUE ) {
		       if ( hFind != INVALID_HANDLE_VALUE ) {
                  printf("Successfully read Object = '%S'\n", fdFile.cFileName);                 
			   } else {
                  printf("Failed to read Object. Volume/NTFS filesystem is corrupt (%d)\n", GetLastError());
               }
			   if ( !FindNextFileW(hFind, &fdFile )) {
                  FindClose(hFind);			
			      hFind = INVALID_HANDLE_VALUE;		
			   }
			}

			::MessageBoxW(NULL, L"Volume mounted under T: drive, press OK to unmount", L"Info", NULL);
            DefineDosDeviceW( DDD_RAW_TARGET_PATH   |
                              DDD_REMOVE_DEFINITION |
                              DDD_EXACT_MATCH_ON_REMOVE, L"T:", sVolumeName.c_str());
         }			           
      }
   }
 
   if (VIX_FAILED(vixError)) {
      printf("Failed at %s:%d with %lld, but ignoring\n",
             __FILE__, __LINE__, vixError);
      vixError = VIX_OK;
   }
   
cleanup:
   printf("Cleanup Stuff:\n");
   VixMntapi_FreeDiskSetInfo(diskSetInfo);
   if (volumeHandles) {
      VixMntapi_FreeVolumeHandles(volumeHandles);
   }
   printf("   Unmounting Volumes...\n"); 
   UnmountVolumes();
   printf("   Unmounting Disks...\n");
   UnmountDisks();
   printf("   Closing Disk handles, unlinking and deleting the child disk file...\n");
   for(j = 0; j < diskHandlesCount; j++) {
      VixDiskLib_Close(diskHandles[j]);
	  VixDiskLib_Unlink(localConnection, childDisks[j].c_str());
	  DeleteFileA(childDisks[j].c_str());
   }   
   VixDiskLib_Disconnect(localConnection);
   printf("Calling VixMntapi_Exit...\n");
   VixMntapi_Exit();
}
