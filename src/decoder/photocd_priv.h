/* Internal decoder interfaces shared across the split implementation units. */

#ifndef PHOTOCD_DECODE_PRIV_H
#define PHOTOCD_DECODE_PRIV_H

#include "photocd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#if defined(__amigaos__) || defined(__AMIGA__) || defined(AMIGA)
#include <dos/dos.h>
#include <proto/dos.h>
#endif

#if defined(__GNUC__) && (__GNUC__ >= 7)
#define PCD_FALLTHROUGH __attribute__((fallthrough))
#else
#define PCD_FALLTHROUGH ((void)0)
#endif

enum pcd_seek_origin {
	PCD_SEEK_SET = 0,
	PCD_SEEK_CUR,
	PCD_SEEK_END
};

#if defined(__amigaos__) || defined(__AMIGA__) || defined(AMIGA)
typedef struct pcd_file pcd_file;
#else
typedef FILE pcd_file;
#endif

void pcd_set_error_string(photocdDecode *self, const char *message);
bool pcd_fail(const char **error, const char *message);
void pcd_set_error_with_suffix(photocdDecode *self, const char *message,
	const char *fallback, const char *suffix);

pcd_file *pcd_file_open(const pcdFilenameType *path);
void pcd_file_close(pcd_file *file);
size_t pcd_file_read(pcd_file *file, void *buffer, size_t size);
int pcd_file_getc(pcd_file *file);
bool pcd_file_seek(pcd_file *file, long offset, int origin);
long pcd_file_tell(pcd_file *file);
size_t pcd_file_read_items(pcd_file *file, void *buffer, size_t item_size,
	size_t item_count);

void pcd_format_version(char *dest, unsigned int major, unsigned int minor);
void pcd_format_scanner_pixel_size(char *dest, const uint8_t *value);

#define KSectorSize 0x800
#define UseFourPixels 1
#define kSceneSectorSize KSectorSize

#if defined(DEBUG)
#define mInformPrintf 1
#endif

#ifdef mNoPThreads
#define kNumThreads 1
#define pcdThreadFunction static void *
#else
#define kNumThreads 8
#ifdef _MSC_VER
#include <windows.h>
#include <process.h>
#define PTHREAD_STACK_MIN 65536
#define pcdThreadDescriptor HANDLE
#define pcdThreadFunction static unsigned __stdcall
#define pthread_attr_t unsigned
#define pthread_attr_init(threadAttr) {(*threadAttr)=PTHREAD_STACK_MIN<<1;}
#define pthread_attr_setstacksize(threadAttr, stackSize) {(*threadAttr)=stackSize;}
#define pthread_attr_setdetachstate(threadAttr, theAttribute) {}
#define pthread_attr_destroy(threadAttr) {}
#define pcdStartThread(theThread, theThreadAttr, theFunction, theData) ((theThread = (HANDLE)_beginthreadex(NULL, theThreadAttr, theFunction, theData, 0, NULL)) == NULL ? -1 : 0)
#define pcdThreadJoin(theThread, result) ((WaitForSingleObject(theThread,INFINITE) != WAIT_OBJECT_0) || !CloseHandle(theThread))
#else
#include <pthread.h>
#include <limits.h>
#define pcdThreadDescriptor pthread_t
#define pcdThreadFunction static void *
#define pcdStartThread(theThread, theThreadAttr, theFunction, theData) pthread_create(&theThread, &theThreadAttr, theFunction, theData)
#define pcdThreadJoin(theThread, result) pthread_join(theThread, result)
#endif
#endif

#ifdef mUseNonGPLCode
pcdThreadFunction upResLumaInterpolatePassI(void *t);
pcdThreadFunction upResLumaInterpolatePassII(void *t);
#endif

extern unsigned int PCDLumaWidth[kMaxScenes];
extern unsigned int PCDLumaHeight[kMaxScenes];
extern unsigned int PCDChromaWidth[kMaxScenes];
extern unsigned int PCDChromaHeight[kMaxScenes];
extern unsigned int PCDChromaResFactor[kMaxScenes];
extern uint32_t RowShift[kMaxScenes];
extern uint32_t RowMask[kMaxScenes];
extern uint32_t RowSubSample[kMaxScenes];
extern uint32_t SequenceShift[kMaxScenes];
extern uint32_t SequenceMask[kMaxScenes];
extern uint32_t PlaneShift[kMaxScenes];
extern uint32_t PlaneMask[kMaxScenes];
extern uint32_t HuffmanHeaderSize[kMaxScenes];

enum PCDOutputDataSize {
	pcdByteSize = 0,
	pcdInt16Size,
	pcdFloatSize
};

struct IPIHeader
{
	char ipiSignature[7];
	uint8_t specificationVersion[2];
	uint8_t authoringSoftwareRelease[2];
	uint8_t imageMagnificationDecriptor[2];
	uint8_t imageScanningTime[4];
	uint8_t imageModificationTime[4];
	uint8_t imageMedium;
	char productType[20];
	char scannerVendorIdentity[20];
	char scannerProductIdentity[16];
	char scannerFirmwareRevision[4];
	char scannerFirmwareDate[8];
	char scannerSerialNumber[20];
	uint8_t scannerPixelSize[2];
	char piwEquipmentManufacturer[20];
	uint8_t photoFinisherCharSet;
	char photoFinisherEscapeSequence[32];
	char photoFinisherName[60];
	char sbaSignature[3];
	uint8_t sbaRevision[2];
	uint8_t sbaCommand;
	uint8_t sbaData[94];
	uint8_t sbaFTN[2];
	uint8_t sbaData2[4];
	uint8_t copyrightStatus;
	char copyrightFile[12];
	uint8_t reservedBytes[1192];
};

struct ImageComponentAttributes {
	uint8_t reserved[2];
	uint8_t attributes;
	uint8_t sectorStop4Base[2];
	uint8_t sectorStop16Base[2];
	uint8_t sectorStopIPE[2];
	uint8_t interleaveRatio;
	uint8_t ADPCMResolution;
	uint8_t ADPCMMagnificationPanning[2];
	uint8_t ADPCMMagnifacationFactor;
	uint8_t ADPCMDisplayOffset[2];
	uint8_t ADPCMTransitionDescriptor;
	uint8_t reserved2[495];
};

struct PCDFileHeader {
	char signature[7];
	uint8_t reserved[2041];
};

struct PCDFile {
	struct PCDFileHeader header;
	struct IPIHeader ipiHeader;
	struct ImageComponentAttributes iciBase16;
	struct ImageComponentAttributes iciBase4;
	struct ImageComponentAttributes iciBase;
	struct ImageComponentAttributes ici4Base;
	struct ImageComponentAttributes ici16Base;
};

typedef struct PCDFile PCDFile;

enum MetaLookUpLengths {
	kMaxPCDFilms = 219,
	kMaxPCDmediums = 10,
	kMaxSBATypes = 4,
	kMaxHuffmanClasses = 4
};

extern const char *PCDMediumTypes[kMaxPCDmediums];
extern const char *PCDSBATypes[kMaxSBATypes];
extern const char *PCDHuffmanClasses[kMaxHuffmanClasses];
extern const short PCDFTN_PC_GC_Medium[kMaxPCDFilms * 4];
extern const char *PCDMediumNames[kMaxPCDFilms];
extern const char *PCDMetadataDescriptions[kMaxPCDMetadata];

#define PCD_FILM_ENTRY(index, field) (PCDFTN_PC_GC_Medium[(index) * 4 + (field)])

enum LUTValues {
	numLUTItems = 1389,
};

extern const uint16_t toLinearLight[numLUTItems];
extern const uint16_t CCIR709tosRGB[numLUTItems];
extern const uint8_t uint8Output[numLUTItems];
extern const uint16_t uint16Output[numLUTItems];
extern const float floatOutput[numLUTItems];

uint16_t getPCD16(uint8_t buffer[]);
uint32_t getPCD32(uint8_t buffer[]);
size_t readBytes(pcd_file *fp, size_t length, uint8_t *data);
int compareBytes(const char *buffer, const char *string);
void copyWithoutPadding(char *dest, const char *src, int length);

struct ReadBuffer
{
	uint8_t sbuffer[KSectorSize];
	pcd_file *fp;
	unsigned long sum;
	unsigned long bits;
	uint8_t *p;
};

typedef struct ReadBuffer ReadBuffer;

struct hctEntry
{
	uint8_t length;
	uint8_t codeWord[2];
	uint8_t key;
};

struct hctTable
{
	uint8_t entries;
	struct hctEntry entry[256];
};

struct huffTable
{
	uint8_t key[0x10000];
	uint8_t len[0x10000];
};

typedef struct huffTable huffTable;

struct huffTables {
	struct huffTable ht[3];
};

typedef struct huffTables huffTables;

#define kHuffmanErrorLen 0x1f

ReadBuffer *pcd_read_buffer_create(pcd_file *file, const char **error);
void pcd_read_buffer_destroy(ReadBuffer *buffer);
bool readAllHuffmanTables(pcd_file *fp, long offset, huffTables *tables,
	int numTables, const char **error);
bool readPCDDeltas(ReadBuffer *buf, struct huffTables *huf, int sceneSelect,
	int sequenceSize, int sequencesToProcess, uint8_t *data[3], off_t colOffset,
	const char **error);

#define pcdMin(x,y) (((x) < (y)) ? x : y)
#define pcdMax(x,y) (((x) < (y)) ? y : x)
#define pcdPin(low, x, high) (((x) < (low)) ? (low) : (((x) > (high)) ? (high) : (x)))

#endif
