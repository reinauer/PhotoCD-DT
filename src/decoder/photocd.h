/* =======================================================
 * PhotoCD decoder - shared Photo CD image decoder interfaces
 * C port for AmigaOS build
 * ======================================================= */

#ifndef PHOTOCD_DECODE_H
#define PHOTOCD_DECODE_H

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#if defined(_USRDLL) && defined(_MSC_VER)
#include <wchar.h>
#endif

#if defined(_USRDLL) && defined(_MSC_VER)
#define pcdMagicAPI __declspec(dllexport)
#define pcdFilenameType wchar_t
#define pcdMagicFOpen _wfopen
#define pcdMagicFOpenMode L"rb"
#define pcdMagicstrlen wcslen
#define pcdMagicstrcpy wcscpy
#define pcdMagicstrcat wcscat
#define pcdMagicstrncpy wcsncpy
#else
#define pcdMagicAPI
#define pcdFilenameType char
#define pcdMagicFOpen fopen
#define pcdMagicFOpenMode "rb"
#define pcdMagicstrlen strlen
#define pcdMagicstrcpy strcpy
#define pcdMagicstrcat strcat
#define pcdMagicstrncpy strncpy
#endif

enum PCDResolutions {
	kBase16 = 0,
	kBase4,
	kBase,
	k4Base,
	k16Base,
	k64Base,
	kMaxScenes
};

enum PCDUpResMethodTags {
	kUpResNearest,
	kUpResIterpolate,
	kUpResLumaIterpolate,
};

enum PCDStringLength {
	kPCDMaxStringLength = 120,
};

enum PCDColorSpaces {
	kPCDRawColorSpace = 0,
	kPCDLinearCCIR709ColorSpace,
	kPCDsRGBColorSpace,
	kPCDYCCColorSpace,
};

enum PCDWhiteBalance {
	kPCDD65White = 0,
	kPCDD50White,
};

enum PCDMetaDataDictionary {
	kspecificationVersion = 0,
	kauthoringSoftwareRelease,
	kimageScanningTime,
	kimageModificationTime,
	kimageMedium,
	kproductType,
	kscannerVendorIdentity,
	kscannerProductIdentity,
	kscannerFirmwareRevision,
	kscannerFirmwareDate,
	kscannerSerialNumber,
	kscannerPixelSize,
	kpiwEquipmentManufacturer,
	kphotoFinisherName,
	ksbaRevision,
	ksbaCommand,
	ksbaFilm,
	kcopyrightStatus,
	kcopyrightFile,
	kcompressionClass,
	kMaxPCDMetadata
};

enum PCDMediums {
	kColorNegative = 0,
	kColorReversal,
	kColorHardcopy,
	kThermalHardcopy,
	kBlackandwhiteNegative,
	kBlackandwhiteReversal,
	kBlackandwhiteHardcopy,
	kinterNegative,
	kSyntheticImage,
	kChromogenic
};

typedef struct photocdDecode {
	int upResMethod;
	bool monochrome;
	uint8_t *luma;
	uint8_t *chroma1;
	uint8_t *chroma2;
	uint8_t *deltas[3][3];
	unsigned int imageRotate;
	unsigned int imageResolution;
	int colorSpace;
	int whiteBalance;
	size_t imageIPEAvailable;
	size_t imageHuffmanClass;
	int baseScene;
	int sceneNumber;
	uint16_t ipeLayers;
	uint16_t ipeFiles;
	void *pcdFileHeader;
	char errorString[kPCDMaxStringLength * 3];
	/* Color conversion LUTs for 68k optimization */
	int16_t y_r[256];
	int16_t y_g[256];
	int16_t y_b[256];
	int16_t c1_r[256];
	int16_t c1_g[256];
	int16_t c1_b[256];
	int16_t c2_r[256];
	int16_t c2_g[256];
	int16_t c2_b[256];
} photocdDecode;

void photocdDecode_init(photocdDecode *self);
void photocdDecode_cleanup(photocdDecode *self);
bool photocdDecode_parseFile(photocdDecode *self, const pcdFilenameType *in_file,
    const pcdFilenameType *ipe_file, unsigned int sNum);
bool photocdDecode_postParse(photocdDecode *self);
unsigned int photocdDecode_getWidth(photocdDecode *self);
unsigned int photocdDecode_getHeight(photocdDecode *self);
bool photocdDecode_isMonochrome(photocdDecode *self);
void photocdDecode_setIsMonoChrome(photocdDecode *self, bool val);
int photocdDecode_getOrientation(photocdDecode *self);
void photocdDecode_setOrientation(photocdDecode *self, unsigned int value);
long photocdDecode_digitisationTime(photocdDecode *self);
bool photocdDecode_monochromeMedia(photocdDecode *self);
void photocdDecode_setInterpolation(photocdDecode *self, int value);
void photocdDecode_setColorSpace(photocdDecode *self, int value);
int photocdDecode_getColorSpace(photocdDecode *self);
void photocdDecode_setWhiteBalance(photocdDecode *self, int value);
char *photocdDecode_getErrorString(photocdDecode *self);
void photocdDecode_getFilmTermData(photocdDecode *self, int *FTN, int *PC, int *GC);
bool photocdDecode_populateFloatBuffers(photocdDecode *self, float *red, float *green,
    float *blue, float *alpha, int d);
bool photocdDecode_populateUInt16Buffers(photocdDecode *self, uint16_t *red,
    uint16_t *green, uint16_t *blue, uint16_t *alpha, int d);
bool photocdDecode_populateUInt8Buffers(photocdDecode *self, uint8_t *red,
    uint8_t *green, uint8_t *blue, uint8_t *alpha, int d);
bool photocdDecode_populateUInt8Row(photocdDecode *self, uint8_t *rgb,
    unsigned int row, unsigned int column, unsigned int width);
void photocdDecode_getMetadata(photocdDecode *self, unsigned int select,
    char *description, size_t desc_len, char *value, size_t val_len);

#endif
