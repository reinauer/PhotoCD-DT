#include "photocd_priv.h"

static int readBaseImage(pcd_file *fp, int sceneNumber,
	off_t ICDOffset[kMaxScenes], uint8_t **luma, uint8_t **chroma1,
	uint8_t **chroma2)
{
	// Base image scene number......
	sceneNumber = (sceneNumber > kBase) ? kBase : sceneNumber;
	bool haveReadBase = false;
	
	while (!haveReadBase && (sceneNumber >= kBase16)) {
		size_t numBytes =
			PCDLumaWidth[sceneNumber] * PCDLumaHeight[sceneNumber] * sizeof(uint8_t) + 1;
		long y;
		size_t count = 0;

		*luma = NULL;
		*chroma1 = NULL;
		*chroma2 = NULL;

		*luma = (uint8_t *)malloc(numBytes);
		*chroma1 = (uint8_t *)malloc(numBytes >> 2);
		*chroma2 = (uint8_t *)malloc(numBytes >> 2);

		if ((*luma == NULL) || (*chroma1 == NULL) || (*chroma2 == NULL) ||
			!pcd_file_seek(fp, (long)(kSceneSectorSize * ICDOffset[sceneNumber]),
				PCD_SEEK_SET)) {
			free(*luma);
			free(*chroma1);
			free(*chroma2);
			sceneNumber--;
			continue;
		}

		for (y = 0; y < (long)PCDChromaHeight[sceneNumber]; y++) {
			count += readBytes(fp, PCDLumaWidth[sceneNumber],
				*luma + y * 2 * PCDLumaWidth[sceneNumber]);
			count += readBytes(fp, PCDLumaWidth[sceneNumber],
				*luma + (y * 2 + 1) * PCDLumaWidth[sceneNumber]);
			count += readBytes(fp, PCDChromaWidth[sceneNumber],
				*chroma1 + y * PCDChromaWidth[sceneNumber]);
			count += readBytes(fp, PCDChromaWidth[sceneNumber],
				*chroma2 + y * PCDChromaWidth[sceneNumber]);
		}
		if (count != ((PCDLumaWidth[sceneNumber] * 2 +
			PCDChromaWidth[sceneNumber] * 2) *
			PCDChromaHeight[sceneNumber])) {
			free(*luma);
			free(*chroma1);
			free(*chroma2);
			sceneNumber--;
			continue;
		}

		haveReadBase = true;
	}
	return sceneNumber;
}

//////////////////////////////////////////////////////////////
//
// Class initialiser and destructors
//
//////////////////////////////////////////////////////////////
static void photocdDecode_freeAll(photocdDecode *self);
static void photocdDecode_discard64BaseDeltas(photocdDecode *self);
static bool photocdDecode_parseICFile(photocdDecode *self,
	const pcdFilenameType *ipe_file);

void photocdDecode_init(photocdDecode *self)
{
	self->luma = NULL;
	self->chroma1 = NULL;
	self->chroma2 = NULL;
	int i, j;
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			self->deltas[i][j] = NULL;
		}
	}
	self->upResMethod = kUpResLumaIterpolate;
	self->pcdFileHeader = NULL;
	self->colorSpace = kPCDRawColorSpace;			// Default for PCD
	self->whiteBalance = kPCDD65White;			// Default for PCD
	self->monochrome = false;
	self->imageIPEAvailable = 0;
	self->imageHuffmanClass = 0;
	self->baseScene = 0;
	self->sceneNumber = 0;
	self->ipeLayers = 0;
	self->ipeFiles = 0;
	self->errorString[0] = 0x0;
	self->imageResolution = 0;
	self->imageRotate = 0;
	// Next line only used if we aren't using static LUTs
//	 populateLUTs();
}

void photocdDecode_cleanup(photocdDecode *self)
{
	photocdDecode_freeAll(self);
}

static void photocdDecode_freeAll(photocdDecode *self)
{
	if (self->luma != NULL) free(self->luma);
	self->luma = NULL;
	if (self->chroma1 != NULL) free(self->chroma1);
	self->chroma1 = NULL;
	if (self->chroma2 != NULL) free(self->chroma2);
	self->chroma2 = NULL;
	if (self->pcdFileHeader != NULL) free(self->pcdFileHeader);
	self->pcdFileHeader = NULL;
	int i, j;
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			if (self->deltas[i][j] != NULL) free(self->deltas[i][j]);
			self->deltas[i][j] = NULL;
		}
	}
}

//////////////////////////////////////////////////////////////
//
// Class accessors
//
//////////////////////////////////////////////////////////////

unsigned int photocdDecode_getWidth(photocdDecode *self)
{
	switch (self->imageRotate) {
		case 0:
			return PCDLumaWidth[self->sceneNumber];
		case 1:
			return PCDLumaHeight[self->sceneNumber];
		case 2:
			return PCDLumaWidth[self->sceneNumber];
		case 3:
			return PCDLumaHeight[self->sceneNumber];
		default:
			return PCDLumaWidth[self->sceneNumber];
	}
}

unsigned int photocdDecode_getHeight(photocdDecode *self)
{
	switch (self->imageRotate) {
		case 0:
			return PCDLumaHeight[self->sceneNumber];
		case 1:
			return PCDLumaWidth[self->sceneNumber];
		case 2:
			return PCDLumaHeight[self->sceneNumber];
		case 3:
			return PCDLumaWidth[self->sceneNumber];
		default:
			return PCDLumaHeight[self->sceneNumber];
	}
}

void photocdDecode_setInterpolation(photocdDecode *self, int value)
{
	self->upResMethod = value;
}

void photocdDecode_setColorSpace(photocdDecode *self, int value)
{
	self->colorSpace = value;
}

int photocdDecode_getColorSpace(photocdDecode *self)
{
	return self->colorSpace;
}

void photocdDecode_setWhiteBalance(photocdDecode *self, int value)
{
	self->whiteBalance = value;
}

char *photocdDecode_getErrorString(photocdDecode *self)
{
	return self->errorString;
}

bool photocdDecode_isMonochrome(photocdDecode *self)
{
	return self->monochrome;
}

void photocdDecode_setIsMonoChrome(photocdDecode *self, bool val) {
	self->monochrome = self->monochrome || val;
}

long photocdDecode_digitisationTime(photocdDecode *self) {
	if (self->pcdFileHeader == NULL) {
		return 0;
	}
	else {
		struct PCDFile *pcdFile = (struct PCDFile *) self->pcdFileHeader;
		return getPCD32(pcdFile->ipiHeader.imageScanningTime);
	}
}

bool photocdDecode_monochromeMedia(photocdDecode *self) {
    if (self->pcdFileHeader == NULL) {
        return false;
    }
    else {
        struct PCDFile *pcdFile = (struct PCDFile *) self->pcdFileHeader;
        if (pcdFile->ipiHeader.imageMedium < kMaxPCDmediums) {
            switch (pcdFile->ipiHeader.imageMedium) {
                case     kBlackandwhiteNegative:
                case     kBlackandwhiteReversal:
                case     kBlackandwhiteHardcopy:
                    return true;

                default:
                    return false;
            }
        }
        else {
            return false;
        }
    }
}

void photocdDecode_getFilmTermData(photocdDecode *self, int *FTN, int *PC, int *GC) {
	struct PCDFile *pcdFile = (struct PCDFile *) self->pcdFileHeader;
	if ((self->pcdFileHeader == NULL) || (compareBytes(pcdFile->ipiHeader.sbaSignature,"SBA")) != 0) {
		*FTN = 0;
		*PC = 0;
		*GC = 0;
	}
	else {
		int index = 0;
		int ftn = getPCD16(pcdFile->ipiHeader.sbaFTN);
		while ((index < kMaxPCDFilms) && (ftn != PCD_FILM_ENTRY(index, 0))) {
			index++;
		}
		if (index >=  kMaxPCDFilms) {
			*FTN = 0;
			*PC = 0;
			*GC = 0;
		}
		else {
			*FTN = PCD_FILM_ENTRY(index, 0);
			*PC = PCD_FILM_ENTRY(index, 1);
			*GC = PCD_FILM_ENTRY(index, 2);
		}
	}	
	return;
}

static void safe_strcpy(char *dest, size_t size, const char *src)
{
	if (size > 0) {
		strncpy(dest, src, size - 1);
		dest[size - 1] = '\0';
	}
}

void photocdDecode_getMetadata(photocdDecode *self, unsigned int select,
    char *description, size_t desc_len, char *value, size_t val_len)
{
	struct PCDFile *pcdFile = (struct PCDFile *) self->pcdFileHeader;
	
	if ((select >= kMaxPCDMetadata) || (self->pcdFileHeader == NULL)) {
		if (description != NULL) safe_strcpy(description, desc_len, "Error");
		safe_strcpy(value, val_len, "Error");
	}
	else {
		if (description != NULL) safe_strcpy(description, desc_len, PCDMetadataDescriptions[select]);
		if (compareBytes(pcdFile->ipiHeader.ipiSignature,"PCD_IPI") == 0) {
			time_t t;
			struct tm *brokentime;
			char *temp;
			switch (select) {
				case kspecificationVersion:
					if (getPCD32(pcdFile->ipiHeader.specificationVersion) == 0xffff) {
						safe_strcpy(value, val_len, "-");
					}
					else {
						pcd_format_version(value, pcdFile->ipiHeader.specificationVersion[0],
							pcdFile->ipiHeader.specificationVersion[1]);
					}
					break;
				case kauthoringSoftwareRelease:		
					if (getPCD32(pcdFile->ipiHeader.authoringSoftwareRelease) == 0xffff) {
						safe_strcpy(value, val_len, "-");
					}
					else {
						pcd_format_version(value,
							pcdFile->ipiHeader.authoringSoftwareRelease[0],
							pcdFile->ipiHeader.authoringSoftwareRelease[1]);
					}
					break;
				case kimageScanningTime:
					// Note we make the glorious assumption that the C library is Posix compliant in that
					// time_t is seconds since 1/1/1970.
					if (getPCD32(pcdFile->ipiHeader.imageScanningTime) == 0xffff) {
						safe_strcpy(value, val_len, "-");
					}
					else {
						t = getPCD32(pcdFile->ipiHeader.imageScanningTime);
						brokentime = localtime(&t);
						temp = asctime(brokentime);
						if (temp) {
							// Get rid of the newline
							temp[strlen(temp) - 1] = 0x0;
							safe_strcpy(value, val_len, temp);
						}
						else {
							safe_strcpy(value, val_len, "-");
						}
					}
					break;
				case kimageModificationTime:
					// Note we make the glorious assumption that the C library is Posix compliant in that
					// time_t is seconds since 1/1/1970
					if (getPCD32(pcdFile->ipiHeader.imageModificationTime) == 0xffff) {
							safe_strcpy(value, val_len, "-");
						}
						else {
							t = getPCD32(pcdFile->ipiHeader.imageModificationTime);
							brokentime = localtime(&t);
							temp = asctime (brokentime);
							if (temp) {
								// Get rid of the newline
								temp[strlen(temp) - 1] = 0x0;
								safe_strcpy(value, val_len, temp);
							}
							else {
								safe_strcpy(value, val_len, "-");
							}
						}
					break;
				case kimageMedium:
					if (pcdFile->ipiHeader.imageMedium < kMaxPCDmediums) {
						safe_strcpy(value, val_len, PCDMediumTypes[pcdFile->ipiHeader.imageMedium]);
					}
					else {
						safe_strcpy(value, val_len, "-");
					}
					break;
				case kproductType:
					copyWithoutPadding(value, pcdFile->ipiHeader.productType, sizeof(pcdFile->ipiHeader.productType));
					break;
				case kscannerVendorIdentity:					
					copyWithoutPadding(value, pcdFile->ipiHeader.scannerVendorIdentity, sizeof(pcdFile->ipiHeader.scannerVendorIdentity));
					break;
				case kscannerProductIdentity:				
					copyWithoutPadding(value, pcdFile->ipiHeader.scannerProductIdentity, sizeof(pcdFile->ipiHeader.scannerProductIdentity));
					break;
				case kscannerFirmwareRevision:				
					copyWithoutPadding(value, pcdFile->ipiHeader.scannerFirmwareRevision, sizeof(pcdFile->ipiHeader.scannerFirmwareRevision));
					break;
				case kscannerFirmwareDate:					
					copyWithoutPadding(value, pcdFile->ipiHeader.scannerFirmwareDate, sizeof(pcdFile->ipiHeader.scannerFirmwareDate));
					break;
				case kscannerSerialNumber:					
					copyWithoutPadding(value, pcdFile->ipiHeader.scannerSerialNumber, sizeof(pcdFile->ipiHeader.scannerSerialNumber));
					break;
				case kscannerPixelSize:	
					// BCD(!) coded
					pcd_format_scanner_pixel_size(value,
						pcdFile->ipiHeader.scannerPixelSize);
					break;
				case kpiwEquipmentManufacturer:				
					copyWithoutPadding(value, pcdFile->ipiHeader.piwEquipmentManufacturer, sizeof(pcdFile->ipiHeader.piwEquipmentManufacturer));
					break;
				case kphotoFinisherName:
					// Don't return anything with a really exotic character set; the chances that
					// it will be displayed correctly are negligible.
					if (pcdFile->ipiHeader.photoFinisherCharSet < 5) {
						copyWithoutPadding(value, pcdFile->ipiHeader.photoFinisherName, sizeof(pcdFile->ipiHeader.piwEquipmentManufacturer));
					}
					else {
						safe_strcpy(value, val_len, "-");
					}
					break;
				case ksbaRevision:	
					if ((compareBytes(pcdFile->ipiHeader.sbaSignature,"SBA") != 0) || (getPCD32(pcdFile->ipiHeader.specificationVersion) == 0xffff)) {
						safe_strcpy(value, val_len, "-");
					}
					else {
						pcd_format_version(value, pcdFile->ipiHeader.specificationVersion[0],
							pcdFile->ipiHeader.specificationVersion[1]);
					}
					break;
				case ksbaCommand:
					if ((compareBytes(pcdFile->ipiHeader.sbaSignature,"SBA") != 0) || (pcdFile->ipiHeader.sbaCommand >= kMaxSBATypes)) {
						safe_strcpy(value, val_len, "-");
					}
					else {
						safe_strcpy(value, val_len, PCDSBATypes[pcdFile->ipiHeader.sbaCommand]);
					}
					break;
				case ksbaFilm:		
					if (compareBytes(pcdFile->ipiHeader.sbaSignature,"SBA") != 0) {
						safe_strcpy(value, val_len, "-");
					}
					else {
						int index = 0;
						int ftn = getPCD16(pcdFile->ipiHeader.sbaFTN);
						while ((index < kMaxPCDFilms) && (ftn != PCD_FILM_ENTRY(index, 0))) {
						   index++;
						}
						if (index >=  kMaxPCDFilms) {
							safe_strcpy(value, val_len, "Unknown film");
						}
						else {
							safe_strcpy(value, val_len, PCDMediumNames[index]);
						}
					}
					break;
				case kcopyrightStatus:
					if (pcdFile->ipiHeader.copyrightStatus == 0x1) {
						safe_strcpy(value, val_len, "Copyright restrictions apply - see copyright file on original CD-ROM for details");	
					}
					else {
						safe_strcpy(value, val_len, "Copyright restrictions not specified");	
					}
					break;
				case kcopyrightFile:
					if (pcdFile->ipiHeader.copyrightStatus == 0x1) {
						copyWithoutPadding(value, pcdFile->ipiHeader.copyrightFile, sizeof(pcdFile->ipiHeader.copyrightFile));
					}
					else {
						safe_strcpy(value, val_len, "-");						
					}
					break;
				case kcompressionClass:		
					safe_strcpy(value, val_len, PCDHuffmanClasses[self->imageHuffmanClass]);
					break;
				default:
					safe_strcpy(value, val_len, "-");
					break;
			}
		}
		else {
			safe_strcpy(value, val_len, "-");
		}
	}
}

//////////////////////////////////////////////////////////////
//
// Structures for the 64Base files
//
//////////////////////////////////////////////////////////////

struct ic_header {
	char ic_name[0x28];
	uint8_t val1[2];
	uint8_t val2[2];
	uint8_t off_descr[4];
	uint8_t off_fnames[4];
	uint8_t off_pointers[4];
	uint8_t off_huffman[4];
};

typedef struct ic_header ic_header;

struct ic_description {
	uint8_t len[2];
	uint8_t color;
	uint8_t fill;
	uint8_t width[2];
	uint8_t height[2];
	uint8_t offset[2];
	uint8_t length[4];
	uint8_t off_pointers[4];
	uint8_t off_huffman[4];
	uint8_t fill2[6];
};

typedef struct ic_description ic_description;


struct ic_fname  {
	char fname[12];
	uint8_t size[4];
};

typedef struct ic_fname ic_fname;

struct ic_entry {
	uint8_t fno[2];
	uint8_t offset[4];
};

typedef struct ic_entry ic_entry;

typedef struct pcd_ic_cleanup {
	pcd_file *thisFile;
	uint8_t *buffer;
} pcd_ic_cleanup;

static bool photocdDecode_parseICFileBody(photocdDecode *self, const pcdFilenameType *ipe_file,
	pcd_file *ic, size_t fileSize, bool usingLowerCase, huffTables *hTables,
	pcd_ic_cleanup *cleanup, const char **error)
{
	struct ic_header *header;
	struct ic_description *description[3];
	struct ic_fname *names[10];
	pcdFilenameType processedFNames[10][13];		// 8.3 plus a terminating char......
	
	ReadBuffer *hufBuffer = NULL;
	uint8_t *buffer;
	int i;

	buffer = (uint8_t *)malloc(fileSize * KSectorSize * sizeof(uint8_t));
	if (buffer == NULL) {
		return pcd_fail(error, "Memory allocation error");
	}
	cleanup->buffer = buffer;
	if (!pcd_file_seek(ic, 0, PCD_SEEK_SET)) {
		return pcd_fail(error, "Unable to seek to 64Base IPE data");
	}
	if (pcd_file_read_items(ic, buffer, KSectorSize, fileSize) < (fileSize - 1)) {
		return pcd_fail(error, "IC File too small");
	}
	header = (ic_header *) buffer;
		self->ipeLayers = getPCD16(buffer + getPCD32(header->off_descr));
		
		if(!((self->ipeLayers==1) || (self->ipeLayers==3))) {
			return pcd_fail(error, "Invalid number of layers");
		}
		
		if (self->monochrome) {
			// Override
			self->ipeLayers = 1;
		}
		// Read the layer descriptions......
		description[0] = (ic_description *) (buffer + getPCD32(header->off_descr) + sizeof(uint16_t));
		description[1] = (ic_description *) (((uint8_t *) description[0]) + getPCD16(description[0]->len));
		description[2] = (ic_description *) (((uint8_t *) description[1]) + getPCD16(description[1]->len));	
		
		// Now read the filenames.....
		self->ipeFiles = getPCD16(buffer + getPCD32(header->off_fnames));
		
		if((self->ipeFiles<1) || (self->ipeFiles>10) || (self->ipeFiles < self->ipeLayers)) {
			return pcd_fail(error, "Invalid number of IPE files");
		}
		
		for (i = 0; i < self->ipeFiles; i++) {
			names[i] = (ic_fname *) (buffer + getPCD32(header->off_fnames) + sizeof(ic_fname)*i + sizeof(uint16_t));
			int j;
			for (j = 0; j < 12; j++) {
				processedFNames[i][j] = (pcdFilenameType) (names[i]->fname[j]);
			}
//			pcdMagicstrncpy(processedFNames[i], names[i]->fname, 12);
			processedFNames[i][12] = (pcdFilenameType) 0x0;
			if (usingLowerCase) {
				unsigned int j;
				for (j = 0; j < pcdMagicstrlen(processedFNames[i]); j++) {
					// Using tolower here is ok; we know the encoding is straight ASCII
					processedFNames[i][j] = tolower(processedFNames[i][j]);
				}
			}
		}
		
		// Read the Huffman tables........
		if (!readAllHuffmanTables(ic, getPCD32(header->off_huffman), hTables,
				self->ipeLayers, error)) {
			photocdDecode_discard64BaseDeltas(self);
			return false;
		}
		
		self->deltas[k64Base - k4Base][0] = (uint8_t *) malloc(PCDLumaWidth[k64Base]*PCDLumaHeight[k64Base]*sizeof(uint8_t));
		if (!self->deltas[k64Base - k4Base][0]) {
			photocdDecode_discard64BaseDeltas(self);
			return pcd_fail(error, "Could not allocate memory for self->deltas");
		}
		else {
			memset(self->deltas[k64Base - k4Base][0], 0x0, PCDLumaWidth[k64Base] * PCDLumaHeight[k64Base] * sizeof(uint8_t));
			if (self->ipeLayers == 3) {
				self->deltas[k64Base - k4Base][1] = (uint8_t*)malloc(PCDChromaWidth[k64Base] * PCDChromaHeight[k64Base] * sizeof(uint8_t));
				if (!self->deltas[k64Base - k4Base][1]) {
					photocdDecode_discard64BaseDeltas(self);
					return pcd_fail(error, "Could not allocate memory for self->deltas");
				}
				else {
					self->deltas[k64Base - k4Base][2] = (uint8_t*)malloc(PCDChromaWidth[k64Base] * PCDChromaHeight[k64Base] * sizeof(uint8_t));
					if (!self->deltas[k64Base - k4Base][2]) {
						photocdDecode_discard64BaseDeltas(self);
						return pcd_fail(error, "Could not allocate memory for self->deltas");
					}
					else {
						memset(self->deltas[k64Base - k4Base][1], 0x0, PCDChromaWidth[k64Base] * PCDChromaHeight[k64Base] * sizeof(uint8_t));
						memset(self->deltas[k64Base - k4Base][2], 0x0, PCDChromaWidth[k64Base] * PCDChromaHeight[k64Base] * sizeof(uint8_t));
					}
				}
			}
		}
			
		int layer;
		int currentFile = 0;
		for(layer = 0; layer< self->ipeLayers; layer++) {
#if defined(DEBUG)
			fprintf(stderr, "len: %d\n", getPCD16((uint8_t*) &description[layer]->len));
			fprintf(stderr, "color: %d\n", description[layer]->color);
			fprintf(stderr, "fill: %d\n", description[layer]->fill);
			fprintf(stderr, "width: %d\n", getPCD16((uint8_t*) &description[layer]->width));
			fprintf(stderr, "height: %d\n", getPCD16((uint8_t*) &description[layer]->height));
			fprintf(stderr, "offset: %d\n", getPCD16((uint8_t*) &description[layer]->offset));
			fprintf(stderr, "length: %d\n", getPCD32((uint8_t*) &description[layer]->length));
			fprintf(stderr, "off_pointers: %d\n", getPCD32((uint8_t*) &description[layer]->off_pointers));
			fprintf(stderr, "off_huffman: %d\n", getPCD32((uint8_t*) &description[layer]->off_huffman));
#endif		
			// Iterate through how ever many sectors there are
			// we pass entire files to the Huffman decoder; all the row and sequence info comes
			// out of the information encoded in the Huffman sequence headers
			int sequenceSize = getPCD32((uint8_t*) &description[layer]->length);
			int numSequences = getPCD16((uint8_t*) &description[layer]->width)*getPCD16((uint8_t*) &description[layer]->height)/sequenceSize;
			int sequence = 0;
			struct ic_entry *entry = (ic_entry *) (buffer + getPCD32((uint8_t*) &description[layer]->off_pointers));
			currentFile = getPCD16((uint8_t*) entry->fno);
			size_t startPoint = getPCD32((uint8_t*) entry->offset);
			while (numSequences-- > 0) {
#if defined(DEBUG)
//				fprintf(stderr, "File No %d, offset %d\n",  getPCD16((uint8_t*) entry->fno), getPCD32((uint8_t*) entry->offset));
#endif
				sequence++;
				if ((currentFile != getPCD16((uint8_t*) entry->fno)) || (numSequences == 0)) {
					pcdFilenameType thisFilePath[512];
					pcdMagicstrcpy(thisFilePath, ipe_file);
					// Truncate the file name part, leaving the path separator
					thisFilePath[pcdMagicstrlen(ipe_file) - 7] = 0;
					pcdMagicstrcat(thisFilePath, processedFNames[currentFile]);
					cleanup->thisFile = pcd_file_open(thisFilePath);
					if (cleanup->thisFile == NULL) {
						return pcd_fail(error, "Could not open 64Base extension image");
					}
					if (!pcd_file_seek(cleanup->thisFile, (long)startPoint, PCD_SEEK_SET)) {
						return pcd_fail(error, "Could not seek 64Base extension image");
					}
					hufBuffer = pcd_read_buffer_create(cleanup->thisFile, error);
					if (hufBuffer == NULL) {
						return false;
					}
					if (!readPCDDeltas(hufBuffer, hTables, k64Base, sequenceSize,
							sequence-1, self->deltas[k64Base - k4Base],
							getPCD16((uint8_t*) &description[layer]->offset), error)) {
						pcd_read_buffer_destroy(hufBuffer);
						return false;
					}
					pcd_read_buffer_destroy(hufBuffer);
					hufBuffer = NULL;
#if defined(DEBUG)
					uint8_t *test = self->deltas[k64Base - k4Base][1];
					test += ((PCDChromaWidth[k64Base]*PCDChromaHeight[k64Base]*sizeof(uint8_t)) >> 1) -32 -224;
#endif					
					pcd_file_close(cleanup->thisFile);
					cleanup->thisFile = NULL;
					currentFile = getPCD16((uint8_t*) entry->fno);
					startPoint = getPCD32((uint8_t*) entry->offset);
					sequence = 0;
				}
				entry++;
			}
		}		
	return true;
}

static void photocdDecode_discard64BaseDeltas(photocdDecode *self)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (self->deltas[k64Base - k4Base][i] != NULL) {
			free(self->deltas[k64Base - k4Base][i]);
			self->deltas[k64Base - k4Base][i] = NULL;
		}
	}
}

static bool photocdDecode_parseICFile(photocdDecode *self, const pcdFilenameType *ipe_file)
{	
	pcd_file *ic;
	huffTables *hTables;
	pcd_ic_cleanup *cleanup;
	const char *error = NULL;

	if (pcdMagicstrlen(ipe_file) < 10) {
		pcd_set_error_string(self, "IPE filename too short to be valid");
		return false;
	}

	ic = pcd_file_open(ipe_file);
	if (ic == NULL) {
		pcd_set_error_string(self, "Could not open 64Base IPE file");
		return false;
	}
	if (!pcd_file_seek(ic, 0, PCD_SEEK_END)) {
		pcd_file_close(ic);
		pcd_set_error_string(self, "Could not read 64Base IPE file");
		return false;
	}

	hTables = (huffTables *)malloc(sizeof(huffTables));
	cleanup = (pcd_ic_cleanup *)calloc(1, sizeof(*cleanup));
	if ((hTables == NULL) || (cleanup == NULL)) {
		free(hTables);
		free(cleanup);
		pcd_file_close(ic);
		pcd_set_error_string(self, "Could not allocate huffman tables");
		return false;
	}

	if (!photocdDecode_parseICFileBody(self, ipe_file, ic,
		((size_t)pcd_file_tell(ic) / KSectorSize) + 1,
		(ipe_file[pcdMagicstrlen(ipe_file) - 9] == 'e'), hTables, cleanup,
		&error)) {
		pcd_set_error_with_suffix(self, error, "Error while processing 64Base image",
			" while processing 64Base image");
		photocdDecode_discard64BaseDeltas(self);
		if (cleanup->thisFile != NULL) {
			pcd_file_close(cleanup->thisFile);
		}
		free(cleanup->buffer);
		free(cleanup);
		free(hTables);
		pcd_file_close(ic);
		return false;
	}
	free(cleanup->buffer);
	free(cleanup);
	free(hTables);
	pcd_file_close(ic);
	return true;
}

static void photocdDecode_discard16BaseDeltas(photocdDecode *self)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (self->deltas[k16Base - k4Base][i] != NULL) {
			free(self->deltas[k16Base - k4Base][i]);
			self->deltas[k16Base - k4Base][i] = NULL;
		}
	}
}

static void photocdDecode_parse16BaseAnd64Base(photocdDecode *self, pcd_file *fp,
	const pcdFilenameType *ipe_file, off_t HCTOffset[kMaxScenes],
	off_t ICDOffset[kMaxScenes], huffTables *hTables)
{
	ReadBuffer *hufBuffer = NULL;
	const char *error = NULL;
	bool ok = true;

	ok = readAllHuffmanTables(fp, kSceneSectorSize * HCTOffset[k16Base], hTables,
		self->monochrome ? 1 : 3, &error);
	if (ok && !pcd_file_seek(fp, (long)(kSceneSectorSize * ICDOffset[k16Base]),
		PCD_SEEK_SET)) {
		ok = pcd_fail(&error, "Unable to seek to 16Base delta data");
	}
	if (ok) {
		self->deltas[k16Base - k4Base][0] =
			(uint8_t *)malloc(PCDLumaWidth[k16Base] * PCDLumaHeight[k16Base] *
			sizeof(uint8_t));
		if (self->deltas[k16Base - k4Base][0] == NULL) {
			ok = pcd_fail(&error, "Memory allocation error");
		}
	}
	if (ok && !self->monochrome) {
		self->deltas[k16Base - k4Base][1] =
			(uint8_t *)malloc(PCDChromaWidth[k16Base] *
			PCDChromaHeight[k16Base] * sizeof(uint8_t));
		self->deltas[k16Base - k4Base][2] =
			(uint8_t *)malloc(PCDChromaWidth[k16Base] *
			PCDChromaHeight[k16Base] * sizeof(uint8_t));
		if (self->deltas[k16Base - k4Base][1] == NULL ||
			self->deltas[k16Base - k4Base][2] == NULL) {
			ok = pcd_fail(&error, "Memory allocation error");
		}
	}
	if (ok) {
		hufBuffer = pcd_read_buffer_create(fp, &error);
		if (hufBuffer == NULL) {
			ok = false;
		} else {
			ok = readPCDDeltas(hufBuffer, hTables, k16Base, 0, 0,
				self->deltas[k16Base - k4Base], 0, &error);
			pcd_read_buffer_destroy(hufBuffer);
		}
	}
	if (ok && self->sceneNumber >= k64Base && !photocdDecode_parseICFile(self, ipe_file)) {
		self->sceneNumber = k16Base;
		if (self->errorString[0] == 0x0) {
			pcd_set_error_string(self, "Error while processing 64Base image");
		}
	}
	if (!ok) {
		self->sceneNumber = k4Base;
		if (self->errorString[0] == 0x0) {
			pcd_set_error_with_suffix(self, error,
				"Could not find a valid 16Base image; falling back to 4Base",
				" while processing 16Base image");
		}
	}
	if (self->sceneNumber == k4Base) {
		photocdDecode_discard16BaseDeltas(self);
	}
}

static void photocdDecode_parse4BaseAndHigher(photocdDecode *self, pcd_file *fp,
	const pcdFilenameType *ipe_file, off_t HCTOffset[kMaxScenes],
	off_t ICDOffset[kMaxScenes])
{
	const char *error = NULL;

	ReadBuffer *hufBuffer = NULL;
	huffTables *hTables = (huffTables *) malloc(sizeof(huffTables));
	bool ok = true;

	if (hTables == NULL) {
		pcd_set_error_string(self, "Could not allocate huffman tables");
		self->sceneNumber = kBase;
		return;
	}

	ok = readAllHuffmanTables(fp, kSceneSectorSize * HCTOffset[k4Base], hTables, 1,
		&error);
	if (ok && !pcd_file_seek(fp, (long)(kSceneSectorSize * ICDOffset[k4Base]),
		PCD_SEEK_SET)) {
		ok = pcd_fail(&error, "Unable to seek to 4Base delta data");
	}
	if (ok) {
		self->deltas[k4Base - k4Base][0] =
			(uint8_t *) malloc(PCDLumaWidth[k4Base] * PCDLumaHeight[k4Base] *
			sizeof(uint8_t));
		if (self->deltas[k4Base - k4Base][0] == NULL) {
			ok = pcd_fail(&error, "Memory allocation error");
		}
	}
	if (ok) {
		hufBuffer = pcd_read_buffer_create(fp, &error);
		if (hufBuffer == NULL) {
			ok = false;
		} else {
			ok = readPCDDeltas(hufBuffer, hTables, k4Base, 0, 0,
				self->deltas[k4Base - k4Base], 0, &error);
			pcd_read_buffer_destroy(hufBuffer);
		}
	}

	if (ok && self->sceneNumber >= k16Base) {
		photocdDecode_parse16BaseAnd64Base(self, fp, ipe_file, HCTOffset,
			ICDOffset, hTables);
	}
	free(hTables);

	if (!ok) {
		self->sceneNumber = kBase;
		if (self->errorString[0] == 0x0) {
			pcd_set_error_with_suffix(self, error,
				"Could not find a valid 4Base image; falling back to Base",
				" while processing 4Base image");
		}
	}
	if (self->sceneNumber == kBase) {
		if (self->deltas[k4Base - k4Base][0] != NULL) {
			free (self->deltas[k4Base - k4Base][0]);
			self->deltas[k4Base - k4Base][0] = NULL;
		}
	}
}

bool photocdDecode_parseFile(photocdDecode *self, const pcdFilenameType *in_file, const pcdFilenameType *ipe_file, unsigned int sNum)
{
	pcd_file *fp = NULL;
	size_t count = 0;
	bool overview;
	struct PCDFile *pcdFile;
	// Final elements of these tables calculated later
	off_t ICDOffset[kMaxScenes]	= {4, 23, 96, 389, 0, 0};
	off_t HCTOffset[kMaxScenes]	= {0, 0, 0, 388, 0, 0};
	
	// Free any memory from previous conversions
	photocdDecode_freeAll(self);
	self->errorString[0] = 0x0;
	
	fp = pcd_file_open(in_file);
	if (fp == NULL)
	{
		pcd_set_error_string(self,
			"Could not open PCD file - may be a file permissions problem");
		return false;
	}
	
	// Check that this is a PCD file.
	self->pcdFileHeader = malloc(sizeof(PCDFile));
	if (self->pcdFileHeader == NULL) {
		pcd_file_close(fp);
		pcd_set_error_string(self, "Could not allocate PCD header buffer");
		return false;
	}
	pcdFile = (struct PCDFile *) self->pcdFileHeader;
	
	count = readBytes(fp, sizeof(PCDFile), (uint8_t *) pcdFile);
	if (count != sizeof(PCDFile)) {
		free(self->pcdFileHeader);
		self->pcdFileHeader = NULL;
		pcd_file_close(fp);
		pcd_set_error_string(self, "PCD file is too small to be valid");
		return false;
	}
	overview = compareBytes(pcdFile->header.signature,"PCD_OPA") == 0;

	if ((compareBytes(pcdFile->ipiHeader.ipiSignature,"PCD_IPI") != 0) && !overview)
	{
		free(self->pcdFileHeader);
		self->pcdFileHeader = NULL;
		pcd_file_close(fp);
		pcd_set_error_string(self, "That is not a valid PCD file");
		return false;
	}

	if (pcdFile->iciBase16.interleaveRatio != 1)
	{
		// We have interleaved audio......
		free(self->pcdFileHeader);
		self->pcdFileHeader = NULL;
		pcd_file_close(fp);
		pcd_set_error_string(self, "The file contains interleaved audio");
		return false;
	}

    photocdDecode_setIsMonoChrome(self, photocdDecode_monochromeMedia(self));
	
	self->imageRotate = pcdFile->iciBase16.attributes & 0x03;
	self->imageResolution = ((pcdFile->iciBase16.attributes >> 2) & 0x03) + kBase;
	self->imageIPEAvailable = (pcdFile->iciBase16.attributes >> 4) & 0x01;
	self->imageHuffmanClass = (pcdFile->iciBase16.attributes >> 5) & 0x02;
	off_t base4Stop = getPCD16(pcdFile->iciBase16.sectorStop4Base);
	// Calculate the file locations that are based of variable sized data
	// See the file description above for why the calculation values
	HCTOffset[k16Base] = base4Stop + 12;
	ICDOffset[k16Base] = base4Stop + 14;
	// unused in this implementation
//	size_t base16Stop = getPCD16(pcdFile->iciBase16.sectorStop16Base);
//	size_t ipeStop = getPCD16(pcdFile->iciBase16.sectorStopIPE);
	
	self->sceneNumber = sNum;
	// Limit the resolution to what we have available
	if (self->imageResolution < k16Base) {
		self->sceneNumber = pcdMin(self->sceneNumber, (int)self->imageResolution);
	}

	// This reads in the base image - may be the right size, may be smaller
	// if smaller, we need to get delta images.........
	self->baseScene = readBaseImage(fp, self->sceneNumber, ICDOffset, &self->luma, &self->chroma1, &self->chroma2);
	
	// Test Image only
//	 genTestBaseImage(self->sceneNumber, self->luma, self->chroma1, self->chroma2);
	
	// Set for what we got now.......
	if (self->baseScene < kBase16) {
		// We couldn't find any image at all
		pcd_file_close(fp);
		pcd_set_error_string(self, "No valid base image could be found");
		return false;
	}
	else if (self->baseScene < kBase) {
		// The image was less than base resolution.....
		// so no delta images can be read
		self->sceneNumber = self->baseScene;
	}
	
	if (self->sceneNumber >= k4Base) {
		photocdDecode_parse4BaseAndHigher(self, fp, ipe_file, HCTOffset, ICDOffset);
	}

	pcd_file_close(fp);
	fp = NULL;
	return true;
}
