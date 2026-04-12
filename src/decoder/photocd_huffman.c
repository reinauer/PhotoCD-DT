#include "photocd_priv.h"


//////////////////////////////////////////////////////////////
//
// Huffman decoder implementation
// This is a standard table-based Huffman decoder
//
//////////////////////////////////////////////////////////////

static bool readHuffTable(struct hctTable *source, struct huffTable *destination,
	int *number, const char **error)
{
	long i;
	struct hctEntry *sub;
	*number=(source->entries)+1;
	for(i=0;i<0x10000;i++) {
		destination->key[i] = 0x7f;
		destination->len[i] = kHuffmanErrorLen;
	}
#if defined(DEBUG)
	fprintf(stderr, "Number of Huffman Tree entries: %d\n", *number);
#endif
	
	for(i=0;i<*number;i++)
	{
		sub = (struct hctEntry *)(((uint8_t *)source)+1+i*sizeof(*sub));
		unsigned int len = (unsigned int) (sub->length + 1);
		if (len > 16) {
			return pcd_fail(error, "Huffman code error!!");
		}
		
#if defined(DEBUG)
//		fprintf(stderr, "Huffman item %d: %x len %d key %x\n", i, getPCD16(sub->codeWord), (unsigned int) (sub->length + 1), sub->key);
#endif
		unsigned int index = 0;
		for (index = 0; index < (0x1u << (16u-len)); index++) {
			uint16_t loc = getPCD16(sub->codeWord) | index;
			destination->key[loc] = sub->key;
			destination->len[loc] = len;
		}
	}

	return true;
}

static int readNextSector(ReadBuffer *buffer)
{
	size_t d;
	size_t n = KSectorSize;
	uint8_t *ptr = buffer->sbuffer;
	if(n == 0) return true;
	for(;;)
	{
		d = pcd_file_read(buffer->fp, ptr, n);
        if( d < 1 ) {
            return false;
        }
		n-=d;
		if (n == 0) break;
		ptr += d;
	}
	return true;
}

static int PCDGetBits(ReadBuffer* b, int n)
{
	b->sum = (b->sum << n) & 0xffffffff;
	b->bits -= n;
	while (b->bits <= 24)
	{
		if (b->p >= (b->sbuffer+KSectorSize))
		{
			if (!readNextSector(b)) {
                // This may not be an error - in ICR files it may just be that the next file is required
                return false;
			}
			b->p = b->sbuffer;
		}
		b->sum |= ((unsigned int) (*b->p) << (24-b->bits));
		b->bits+=8;
		b->p++;
	}
    return true;
}

ReadBuffer *pcd_read_buffer_create(pcd_file *file, const char **error)
{
	ReadBuffer *buffer;

	buffer = (ReadBuffer *)malloc(sizeof(*buffer));
	if (buffer == NULL) {
		pcd_fail(error, "memory allocation error");
		return NULL;
	}

	buffer->fp = file;
	buffer->p = buffer->sbuffer + sizeof(buffer->sbuffer);
	buffer->bits = 0;
	buffer->sum = 0;

	// Initialise the shift register
    if (!PCDGetBits(buffer, 0)) {
		pcd_read_buffer_destroy(buffer);
        pcd_fail(error,
			"Unexpected end of file while initializing read buffer");
		return NULL;
    }

	return buffer;
}

void pcd_read_buffer_destroy(ReadBuffer *buffer)
{
	free(buffer);
}

static int syncHuffman(ReadBuffer* b)
{
	while (!((b->sum & 0x00fff000) == 0x00fff000)) {
        if (!PCDGetBits(b, 8)) {
            return false;;
        }
	}
	while (!((b->sum & 0xffffff00) == 0xfffffe00)) {
        if (!PCDGetBits(b, 1)) {
            return false;
        }
	}
#if defined(DEBUG)
//		fprintf(stderr, "Sync at : %d %d ftell:%d -> ", ftell(b->fp));
#endif
    return true;
}

static bool PCDDecodeHuffman(ReadBuffer* b, struct huffTable *huf, uint8_t *dest,
	int length, const char **error)
{
	int i;
	uint16_t code;
	uint8_t *ptr = dest;
	
	for (i = 0; i < length; i++) {
		code  = (b->sum >> 16) & 0xffff;
		if (huf->len[code] == kHuffmanErrorLen) {
#ifdef mInformPrintf
			fprintf(stderr, "*** Warning : Attempting to recover from Huffman sequence error......\n");
#endif
#if defined(DEBUG)
			// If we're debugging, then this is almost certainly our own error
			return pcd_fail(error, "Huffman code sequence error");
#else
            // Recovery procedure from error is to zero this sequence and just go on
            // to the next - as these are deltas, we just lose one sequence of
            // incremental information
            for (i = 0; i < length; i++) {
                *dest++ = 0x0;
            }
            if (!syncHuffman(b)) {
                return pcd_fail(error,
					"Unexpected end of file while attempting recover from an error in the Huffman sequence");
            }
            return true;
#endif
		}
		else {
			*ptr++ = huf->key[code];
            if (!PCDGetBits(b, huf->len[code])) {
                return false;
            }
		}
	}
    return true;
}

bool readAllHuffmanTables(pcd_file *fp, long offset, huffTables *tables,
	int numTables, const char **error)
{
	int numBytes = kSceneSectorSize * (numTables == 1 ? 1 : 2) * sizeof(uint8_t);
	uint8_t *buffer = (uint8_t *) malloc(numBytes);
	
	if (buffer == NULL) {
		return pcd_fail(error, "memory allocation error");
	}
	
	if (!pcd_file_seek(fp, offset, PCD_SEEK_SET)) {
		free(buffer);
		return pcd_fail(error, "Unable to seek to Huffman table");
	}
	if (pcd_file_read(fp, buffer, (size_t)numBytes) != (size_t)numBytes) {
		free(buffer);
		return pcd_fail(error, "Unable to read Huffman table");
	}

	int num = 0;
	int i;
	uint8_t *ptr = buffer;
	// Read in the Huffman decoder tables, and process into something we can use
	for (i = 0; i < numTables; i++) {
		
#if defined(DEBUG)
		fprintf(stderr, "Processing Table number: %i\n", i);
#endif
		if (!readHuffTable((struct hctTable *)ptr, &(tables->ht[i]), &num, error)) {
			free(buffer);
			return false;
		}
		// Move the pointer formward by the size of what we just read
		ptr+= sizeof(uint8_t)*(num*4 + 1);
		if ((num < 4) && (i > 0)) {
			// Assume the previous table applies(!)
			memcpy( &(tables->ht[i]), &(tables->ht[i-1]), sizeof(huffTable));
		}
	}
#if defined(DEBUG)
	uint8_t eptDescriptor = *ptr++;
	fprintf(stderr, "EPT descriptor: %x\n", eptDescriptor);
#endif
	free(buffer);
	return true;
}


//////////////////////////////////////////////////////////////
//
// Reader for the delta tables - this supports base, 16 base
// and 64Base
//
//////////////////////////////////////////////////////////////

bool readPCDDeltas(ReadBuffer *buf, struct huffTables *huf, int sceneSelect,
	int sequenceSize, int sequencesToProcess, uint8_t *data[3], off_t colOffset,
	const char **error)
{		
	size_t count;
	unsigned long plane, row;
    unsigned int sequence, sequenceMax;
	int lumaSequenceSize;
	int chromaSequenceSize;
	int planeTrack = ((data[0] != NULL) ? 0x1 : 0) | ((data[1] != NULL) ? 0x2 : 0) | ((data[2] != NULL) ? 0x4 : 0);
	
	if (sequencesToProcess == 0) {
		// for anything less than 64base, one sequence per row
		sequencesToProcess = (sceneSelect == k64Base) ? 1 : PCDLumaHeight[sceneSelect] + 2*PCDChromaHeight[sceneSelect];
	}

	
	plane = 0;
	row = 0;
	sequence = 0;
    sequenceMax = 0;
	lumaSequenceSize = sequenceSize == 0 ? (int)PCDLumaWidth[sceneSelect] : sequenceSize;
	chromaSequenceSize = sequenceSize == 0 ? (int)PCDChromaWidth[sceneSelect] : sequenceSize;
    // we need to check that we're at the start of a sequence with syncHuffman(buf);
    // if syncHuffman comes back false, that's not necesarily an error; we might just need to move to the next file
    while (((planeTrack != 0x0) || (row < PCDLumaHeight[sceneSelect])) && (sequencesToProcess > 0) && syncHuffman(buf)) {
		// Get the first 24 bits into the shift register - these have the plane, row and sequence numbers
        if (!PCDGetBits(buf, 16)) {
            return pcd_fail(error,
				"Unexpected end of file while attempting to read plane, row and sequence numbers");
        }
		row = (buf->sum >> RowShift[sceneSelect]) & RowMask[sceneSelect];
		sequence = (buf->sum >> SequenceShift[sceneSelect]) & SequenceMask[sceneSelect];
		plane = (buf->sum >> PlaneShift[sceneSelect]) & PlaneMask[sceneSelect];
		row *= (plane == 0 ? 1 : RowSubSample[sceneSelect]);
        sequenceMax = sequence > sequenceMax ? sequence : sequenceMax;
		
#if defined(DEBUG)
 //       fprintf(stderr, "Row %lu, Sequence %d, data:%lx\n",  row, sequence, buf->sum);
#endif
		
		for (count = 0; count < HuffmanHeaderSize[sceneSelect]; count++) {
			// IPE headers have 32 bits of data
            if (!PCDGetBits(buf, 8)) {
                return pcd_fail(error,
					"Unexpected end of file while attempting to read IPE headers");
            }
		}

		if (row < PCDLumaHeight[sceneSelect]) {
#if defined(DEBUG)
//            fprintf(stderr, "Delta plane: %lu row: %lu\n", plane, row);
#endif
 			switch (plane)
			{
				case 0:
				{
					if (!PCDDecodeHuffman(buf,
									 &(huf->ht[0]),
									 data[0] + row*PCDLumaWidth[sceneSelect] + sequence*sequenceSize + colOffset,
                                     lumaSequenceSize, error)) {
                        return false;
                    }
					planeTrack &= 0x6;
					break;
				}
				case 2:
				{
					if (data[1] != NULL) {
                        if (!PCDDecodeHuffman(buf,
									 &(huf->ht[1]),
									 data[1]+(row>>1)*PCDChromaWidth[sceneSelect] + sequence*sequenceSize + (colOffset>>1),
                                              chromaSequenceSize, error)) {
                            return false;

                        }
					}
					planeTrack &= 0x5;
					break;
				}
				case 3:
				// Handle the strange IPE situation - plane numbers are different(!)
				case 4:
				{
					if (data[2] != NULL) {
                        if (!PCDDecodeHuffman(buf,
									 &(huf->ht[2]),
									 data[2]+(row>>1)*PCDChromaWidth[sceneSelect] + sequence*sequenceSize + (colOffset>>1),
                                              chromaSequenceSize, error)) {
                            return false;
                        }
					}
					planeTrack &= 0x3;
					break;
				}
				default:
				{
					return pcd_fail(error, "Corrupt Image");
				}
			}
		}
		else {
#if defined(DEBUG)
			fprintf(stderr, "Delta plane invalid row: %ld row: %ld\n", plane, row);
#endif
		}
        // Prevent cutting off a sequence early
        if (!(sequence < sequenceMax && sequencesToProcess == 1)) {
            sequencesToProcess--;
        }
	}
	return(true);		
}
