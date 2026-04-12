#include "photocd_priv.h"

struct upResInterpolateData {
	uint8_t *base;
	uint8_t *dest;
	uint8_t *luma;
	unsigned int width;
	unsigned int height;
	bool hasDeltas;
	unsigned int startRow;
	unsigned int endRow;
};

//////////////////////////////////////////////////////////////
//
// basic "Kodak standard" bilinear upres interpolator
//
//////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////
//
// basic "Kodak standard" bilinear upres interpolator
//
//////////////////////////////////////////////////////////////
pcdThreadFunction upResInterpolate(void *t)
{
	struct upResInterpolateData *rd = (struct upResInterpolateData *) t;
	unsigned int row, column;
	ptrdiff_t indexDelta;
	int sum;
	int8_t *deltaBase = (int8_t *) rd->dest;
	
	// This is as intended by Kodak - linear interpolation
	uint8_t *basePix, *basePix01, *basePix10, *basePix11;
	unsigned int rowPlus, columnPlus;
	for (row = rd->startRow>>1; row < rd->endRow>>1; row++) {
		for (column = 0; column < rd->width>>1; column++) {
			// When upresing, the factor is always two
			// Note here we're iterating in rd->base coordinates
			columnPlus = pcdMin(column + 1, (rd->width>>1)-1);
			rowPlus = pcdMin(row + 1, (rd->height>>1)-1);
			basePix = rd->base + column + row * (rd->width>>1);
			basePix01 = rd->base + columnPlus + row * (rd->width>>1);					
			basePix10 = rd->base + column + rowPlus * (rd->width>>1);		
			basePix11 = rd->base + columnPlus + rowPlus * (rd->width>>1);	
			
			// base Pixel
			indexDelta = (column<<1) + (row<<1) * rd->width;
			sum = (int) (*basePix);
			if (rd->hasDeltas) sum += (int) (*(deltaBase + indexDelta));
			sum = sum < 0 ? 0 : (sum > 255 ? 255 : sum);
			*(rd->dest + indexDelta) = (uint8_t) sum;
			
			// 01 Pixel
			indexDelta = (column<<1) + 1 + (row<<1) * rd->width;
			sum = ((int) (*basePix) + (int) (*basePix01) + 1) >> 1;
			if (rd->hasDeltas) sum += (int) (*(deltaBase + indexDelta));
			sum = sum < 0 ? 0 : (sum > 255 ? 255 : sum);
			*(rd->dest + indexDelta) = (uint8_t) sum;
			
			// 10 Pixel		
			indexDelta = (column<<1) + ((row<<1) + 1) * rd->width;
			sum = ((int) (*basePix) + (int) (*basePix10) + 1) >> 1;
			if (rd->hasDeltas) sum += (int) (*(deltaBase + indexDelta));
			sum = sum < 0 ? 0 : (sum > 255 ? 255 : sum);
			*(rd->dest + indexDelta) = (uint8_t) sum;
			
			// 11 Pixel
			indexDelta = (column<<1) + 1 + ((row<<1) + 1) * rd->width;
#ifdef UseFourPixels
			sum = ((int) (*basePix) + (int) (*basePix01) + (int) (*basePix10) + (int) (*basePix11) + 2) >> 2;
#else
			sum = ((int) (*basePix) + (int) (*basePix11) + 1) >> 1;
#endif
			if (rd->hasDeltas) sum += (int) (*(deltaBase + indexDelta));
			sum = sum < 0 ? 0 : (sum > 255 ? 255 : sum);
			*(rd->dest + indexDelta) = (uint8_t) sum;
			
		}
	}			
	return NULL;
}

#ifdef mUseNonGPLCode
#include "PCDLumaInterpolate.hpp"
#endif


static void upResBuffer(uint8_t *base, uint8_t *dest, uint8_t *luma, unsigned int width, unsigned int height, int upResMethod, bool hasDeltas)
{
	unsigned int row, column;
	ptrdiff_t indexBase, indexDelta;
	int sum, thread;
	int previousRow = 0;
#ifndef mNoPThreads
	int rc;
	void *status;
	pcdThreadDescriptor threadDescriptors[kNumThreads];
	pthread_attr_t threadAttr;
	
	/* Initialize and set thread detached attribute */
	pthread_attr_init(&threadAttr);
	pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_JOINABLE);
	// Use minimum stacksize times two; we have only a few stack variables
	pthread_attr_setstacksize(&threadAttr, PTHREAD_STACK_MIN<<1);
#endif

	
	if (dest != NULL) {
#ifdef mUseNonGPLCode
		if ((upResMethod >= kUpResLumaIterpolate) && !hasDeltas && (luma != NULL)) {

			// This does a homogeniety minimisation routine.
			// We should only ever(!) use this for chroma interpolation
			struct upResInterpolateData rd[kNumThreads];
			for (thread = 0; thread < kNumThreads; thread++) {
				rd[thread].base = base;
				rd[thread].dest = dest;
				rd[thread].luma = luma;
				rd[thread].width = width;
				rd[thread].height = height;
				rd[thread].hasDeltas = hasDeltas;
				rd[thread].startRow = previousRow;
				rd[thread].endRow =height/kNumThreads*(thread+1);
				previousRow = rd[thread].endRow;
#ifndef mNoPThreads
				if (thread == (kNumThreads - 1)) {
					upResLumaInterpolatePassI(&(rd[thread]));
				}
				else {
					if (pcdStartThread(threadDescriptors[thread], threadAttr, upResLumaInterpolatePassI, (void *)&(rd[thread])) != 0) {
						// Too many threads already.....
						upResLumaInterpolatePassI(&(rd[thread]));
						// Don't try to join
						rd[thread].endRow = 0;
					}
				}
#else
				upResLumaInterpolatePassI(&(rd[thread]));
#endif
			}
#ifndef mNoPThreads
			pthread_attr_destroy(&threadAttr);
			for (thread = 0; thread < (kNumThreads-1); thread++) {
				if (rd[thread].endRow > 0) {
					rc = pcdThreadJoin(threadDescriptors[thread], &status);
				}
			}
#endif
			
			// For this algorithm, the easist thing is to prep the last rows and columns separately.....
			for (row = height-1; row < height; row++) {
				for (column = 0; column < width; column++) {
					*(dest + column + row * (width)) = *(base + (column>>1) + (row>>1) * (width>>1));
				}
			}
			for (row = 0; row < height; row++) {
				for (column = column-1; column < width; column++) {
					*(dest + column + row * (width)) = *(base + (column>>1) + (row>>1) * (width>>1));
				}
			}
			
			
			for (thread = 0; thread < kNumThreads; thread++) {
#ifndef mNoPThreads
				if (thread == (kNumThreads - 1)) {
					upResLumaInterpolatePassII(&(rd[thread]));
				}
				else {
					if (pcdStartThread(threadDescriptors[thread], threadAttr, upResLumaInterpolatePassII, (void *)&(rd[thread])) != 0) {
						// Too many threads already.....
						upResLumaInterpolatePassII(&(rd[thread]));
						// Don't try to join
						rd[thread].endRow = 0;
					}
				}
#else
				upResLumaInterpolatePassII(&(rd[thread]));
#endif
			}
#ifndef mNoPThreads
			pthread_attr_destroy(&threadAttr);
			for (thread = 0; thread < (kNumThreads-1); thread++) {
				if (rd[thread].endRow > 0) {
					rc = pcdThreadJoin(threadDescriptors[thread], &status);
				}
			}
#endif

		}
		else
#endif
		if (upResMethod >= kUpResIterpolate) {
			struct upResInterpolateData rd[kNumThreads];
			for (thread = 0; thread < kNumThreads; thread++) {
				rd[thread].base = base;
				rd[thread].dest = dest;
				rd[thread].luma = luma;
				rd[thread].width = width;
				rd[thread].height = height;
				rd[thread].hasDeltas = hasDeltas;
				rd[thread].startRow = previousRow;
				rd[thread].endRow =height/kNumThreads*(thread+1);
				previousRow = rd[thread].endRow;
#ifndef mNoPThreads
				if (thread == (kNumThreads - 1)) {
					upResInterpolate(&(rd[thread]));
				}
				else {
					if (pcdStartThread(threadDescriptors[thread], threadAttr, upResInterpolate, (void *)&(rd[thread])) != 0) {
						// Too many threads already.....
						upResInterpolate(&(rd[thread]));
						// Don't try to join
						rd[thread].endRow = 0;
					}
				}
#else
				upResInterpolate(&(rd[thread]));
#endif
			}
#ifndef mNoPThreads
			pthread_attr_destroy(&threadAttr);
			status  = 0; // Avoid unreferenced local variable warning
			for (thread = 0; thread < (kNumThreads-1); thread++) {
				if (rd[thread].endRow > 0) {
					rc = pcdThreadJoin(threadDescriptors[thread], &status);
				}
			}
#endif

		}
		else {
			// Here we do a very simple minded nearest neighbour look up;
			// Shouldn't be used for any serious purpose.
			int8_t *deltaBase = (int8_t *) dest;
			for (row = 0; row < height; row++) {
				for (column = 0; column < width; column++) {
					// When upresing, the factor is always two
					indexBase = (column >> 1) + (row >> 1) * (width>>1);
					indexDelta = column + row * width;
					sum = ((int) *(base + indexBase));
					if (hasDeltas) {
						sum += ((int) *(deltaBase + indexDelta));
						sum = sum < 0 ? 0 : (sum > 255 ? 255 : sum);
					}

					*(dest + indexDelta) = (uint8_t) sum;
				}
			}
		}
		// Now the new base is in the old dest....
	}
	
}

//////////////////////////////////////////////////////////////
//
// Test code
//
//////////////////////////////////////////////////////////////

#if defined(DEBUG)
void dump8by8(uint8_t *b, int width)
{
	fprintf(stderr, "\nDump8x8x\n");
	int i, j;
	for (i=0; i<8; i++) {
		for (j=0; j<8; j++) {
			uint8_t val = *(b+j+i*width);
			fprintf(stderr, " %x", val);
		}
		fprintf(stderr, "\n");
	}
}

void dumpColumn(uint8_t *b, int col, int height, int width)
{
	fprintf(stderr, "\nDumpColumn\n");
	int i;
	for (i=0; i<height; i++) {
		uint8_t val = *(b+col+i*width);
		fprintf(stderr, " %x\n", val);
	}
}

void genTestBaseImage(int sceneNumber, uint8_t *luma, uint8_t *chroma1, uint8_t *chroma2)
{
	// Base image scene number......
	
	int row, column;
	
	for (row = 0; row < PCDLumaHeight[sceneNumber]; row++) {
		for (column = 0; column < PCDLumaWidth[sceneNumber]; column++) {
			bool block = ((row & 0x3) < 2) && ((column & 0x3) < 2);
			if (block) {
				*luma++ = 0xff;
			}
			else {
				*luma++ = 0x3f;
			}
			if (((row & 0x1) == 0x0) && ((column & 0x1) == 0x0)) {
				// write chroma
				// 156 and 137 are the "zero" values
				if (block) {
					*chroma1++ = 230;
					*chroma2++ = 230;
				}
				else {
					*chroma1++ = 156;
					*chroma2++ = 137;
				}
			}
		}
	}	
}

#endif

//////////////////////////////////////////////////////////////
//
// RGB conversion
//
//////////////////////////////////////////////////////////////

struct ConvertToRGBData {
	short outputSize;
	void *red;
	void *green;
	void *blue;
	void *alpha;
	ptrdiff_t d;
	size_t startRow;
	size_t endRow;
	size_t columns;
	size_t rows;
	uint8_t *lp;
	uint8_t *c1p;
	uint8_t *c2p;
	unsigned int resFactor;
	unsigned int imageRotate;
	size_t colorSpace;
	int whiteBalance;
};


//////////////////////////////////////////////////////////////
//
// The Micro CMM
//
//////////////////////////////////////////////////////////////
//
// Normally, what we would do is to define a Photo CD color space, then
// hand that together with the data to a Color Management Module - e.g.,
// LittleCMS - and let it deal with all the nasty complex color space conversions.
// However, to keep this as standalone as possible, what is implemented here is
// a "micro CMM", in the shape of a few LUTs and one matrix conversion. It does
// everything that a full CMM would do.....admittedly, only for the limited
// Photo CD to linear light to sRGB conversions that we need. And all in integer
// math. And multi-threaded.
//
pcdThreadFunction convertToRGB(void *t)
{
	struct ConvertToRGBData *rd = (struct ConvertToRGBData *) t;
	size_t row = 0;
	size_t col = 0;
	int32_t Li = 0, C1i = 0, C2i = 0, ri = 0, gi = 0, bi = 0;
	int32_t rt = 0, gt = 0, bt = 0;
	ptrdiff_t chromaIndex = 0, lumaIndex = 0, destIndex = 0;
	
	for (row = rd->startRow; row != rd->endRow; row++) {
		for (col = 0; col != rd->columns; col++) {
			switch (rd->imageRotate) {
				case 0:
					destIndex = (col + row*rd->columns)*rd->d;
					break;
				case 1:
					destIndex = (row + (rd->columns - 1 - col)*rd->rows)*rd->d;
					break;
				case 2:					
					destIndex = (rd->columns - 1 - col + (rd->rows - 1 - row)*rd->columns)*rd->d;
					break;
				case 3:
					destIndex = (rd->rows - 1 - row + col*rd->rows)*rd->d;
					break;
				default:
					destIndex = (col + row*rd->columns)*rd->d;
					break;
			}
			lumaIndex = col + row * rd->columns;
			chromaIndex = (col>>rd->resFactor) + (row >> rd->resFactor) * (rd->columns >> rd->resFactor);
			
			if (rd->colorSpace == kPCDYCCColorSpace) {
				// Here we want the original YCC color space
				ri = pcdPin(0, (((int32_t) *(rd->lp + lumaIndex))<<10)/188, 1388);
				gi = pcdPin(0, (((int32_t) (rd->c1p)[chromaIndex])<<10)/188, 1388);
				bi = pcdPin(0, (((int32_t) (rd->c2p)[chromaIndex])<<10)/188, 1388);
			}
			else {
				// here one or the other of the RGB color spaces
				Li = *(rd->lp + lumaIndex) * 5573;								// Range 0 - 1,421,115
				if (rd->c1p != NULL) {
					C1i = (((int32_t) (rd->c1p)[chromaIndex]) - 156) * 9085;	// -1,417,260 to 899,415
				}
				if (rd->c2p != NULL) {
					C2i = (((int32_t) (rd->c2p)[chromaIndex]) - 137) * 7461;	// -1,022,157 to 880,398
				}
				ri = pcdPin(0, (Li + C2i) >> 10, 1388);							// 0 - 1388
				gi = pcdPin(0, (Li>>10) - C1i/5278 - C2i/2012, 1388);			// 0 - 1388
				bi = pcdPin(0, (Li + C1i) >> 10, 1388);							// 0 - 1388
			
				// Here we have RGB in the original photo CD color space. So we can either
				// (a) pass that back raw, or
				// (b) convert to a CCIR709 linear light space, or
				// (c) convert to a sRGB space
				if ((rd->colorSpace == kPCDLinearCCIR709ColorSpace) || (rd->colorSpace == kPCDsRGBColorSpace)) {
					ri = toLinearLight[ri];
					gi = toLinearLight[gi];
					bi = toLinearLight[bi];
					// We only do whitebalance conversions for the processed spaces, not raw.....
					if (rd->whiteBalance == kPCDD50White) {
						// This implements the equivalent of:
						//	r = (0.9555f*r-0.0231f*g+0.0633f*b)/1.32;
						//	g = (-0.0283f*r+1.0100f*g+0.0211*b)/1.32;
						//	p = (0.0123f*r-0.0206f*g+1.3303f*b)/1.32;
						rt = ri;
						gt = gi;
						bt = bi;
						ri = (5930*rt - 143*gt + 393*bt)>>13;
						gi = (-176*rt + 6268*gt + 131*bt)>>13;
						bi = (76*rt - 128*gt + 8256*bt)>>13;
					}
				}
				if (rd->colorSpace == kPCDsRGBColorSpace) {
					// Recompress and pin
					ri = CCIR709tosRGB[pcdPin(0, ri, 1388)];
					gi = CCIR709tosRGB[pcdPin(0, gi, 1388)];
					bi = CCIR709tosRGB[pcdPin(0, bi, 1388)];
				}
				else {
					// just pin
					ri = pcdPin(0, ri, 1388);
					gi = pcdPin(0, gi, 1388);
					bi = pcdPin(0, bi, 1388);
				}
			}
			// Deliver back in the right format
			if (rd->outputSize == pcdFloatSize) {
				*(((float *) rd->red) + destIndex) = floatOutput[ri];
				*(((float *) rd->green) + destIndex) = floatOutput[gi];
				*(((float *) rd->blue) + destIndex) = floatOutput[bi];
				if (rd->alpha != NULL) *(((float *) rd->alpha) + destIndex) = 1.0f;
			}
			else if (rd->outputSize == pcdInt16Size) {
				*(((uint16_t *) rd->red) + destIndex) = uint16Output[ri];
				*(((uint16_t *) rd->green) + destIndex) = uint16Output[gi];
				*(((uint16_t *) rd->blue) + destIndex) = uint16Output[bi];
				if (rd->alpha != NULL) *(((uint16_t *) rd->alpha) + destIndex) = 0xffff;
			}
			else {
				*(((uint8_t *) rd->red) + destIndex) = uint8Output[ri];
				*(((uint8_t *) rd->green) + destIndex) = uint8Output[gi];
				*(((uint8_t *) rd->blue) + destIndex) = uint8Output[bi];
				if (rd->alpha != NULL) *(((uint8_t *) rd->alpha) + destIndex) = 0xff;					
			}
		}
	}
	return NULL;
}

static void photocdDecode_mapOutputToSource(const photocdDecode *self,
	unsigned int out_x, unsigned int out_y, unsigned int *src_x,
	unsigned int *src_y)
{
	unsigned int source_width = PCDLumaWidth[self->sceneNumber];
	unsigned int source_height = PCDLumaHeight[self->sceneNumber];

	switch (self->imageRotate & 3U) {
		case 1:
			*src_x = source_width - 1U - out_y;
			*src_y = out_x;
			break;
		case 2:
			*src_x = source_width - 1U - out_x;
			*src_y = source_height - 1U - out_y;
			break;
		case 3:
			*src_x = out_y;
			*src_y = source_height - 1U - out_x;
			break;
		case 0:
		default:
			*src_x = out_x;
			*src_y = out_y;
			break;
	}
}

static uint8_t photocdDecode_sampleChroma(const uint8_t *plane, unsigned int width,
	unsigned int height, unsigned int x, unsigned int y, int method)
{
	unsigned int base_x;
	unsigned int base_y;
	unsigned int next_x;
	unsigned int next_y;
	uint8_t p00;

	if (plane == NULL) {
		return 0;
	}

	base_x = x >> 1;
	base_y = y >> 1;
	p00 = plane[base_x + base_y * width];
	if (method < kUpResIterpolate) {
		return p00;
	}

	next_x = pcdMin(base_x + 1U, width - 1U);
	next_y = pcdMin(base_y + 1U, height - 1U);

	if ((x & 1U) == 0U && (y & 1U) == 0U) {
		return p00;
	}
	if ((x & 1U) != 0U && (y & 1U) == 0U) {
		return (uint8_t)(((unsigned int)p00 +
			(unsigned int)plane[next_x + base_y * width] + 1U) >> 1);
	}
	if ((x & 1U) == 0U) {
		return (uint8_t)(((unsigned int)p00 +
			(unsigned int)plane[base_x + next_y * width] + 1U) >> 1);
	}

	return (uint8_t)(((unsigned int)p00 +
		(unsigned int)plane[next_x + next_y * width] + 1U) >> 1);
}

static void photocdDecode_prepareLUTs(photocdDecode *self)
{
	int i;
	for (i = 0; i < 256; i++) {
		int32_t Li = i * 5573;
		int32_t C1i = (i - 156) * 9085;
		int32_t C2i = (i - 137) * 7461;

		self->y_r[i] = (int16_t)pcdPin(0, Li >> 10, 1388);
		self->y_g[i] = (int16_t)pcdPin(0, Li >> 10, 1388);
		self->y_b[i] = (int16_t)pcdPin(0, Li >> 10, 1388);
		self->c1_r[i] = 0;
		self->c1_g[i] = (int16_t)-(C1i / 5278);
		self->c1_b[i] = (int16_t)(C1i >> 10);
		self->c2_r[i] = (int16_t)(C2i >> 10);
		self->c2_g[i] = (int16_t)-(C2i / 2012);
		self->c2_b[i] = 0;
	}
}

static void photocdDecode_convertPixel(photocdDecode *self, uint8_t luma_value,
	uint8_t chroma1_value, uint8_t chroma2_value, bool has_chroma, int32_t *ri,
	int32_t *gi, int32_t *bi)
{
	int32_t rt = 0;
	int32_t gt = 0;
	int32_t bt = 0;

	if (self->colorSpace == kPCDYCCColorSpace) {
		*ri = pcdPin(0, (((int32_t)luma_value) << 10) / 188, 1388);
		*gi = pcdPin(0, (((int32_t)(has_chroma ? chroma1_value : 156)) << 10) / 188,
			1388);
		*bi = pcdPin(0, (((int32_t)(has_chroma ? chroma2_value : 137)) << 10) / 188,
			1388);
		return;
	}

	*ri = pcdPin(0, self->y_r[luma_value] + (has_chroma ? self->c2_r[chroma2_value] : 0), 1388);
	*gi = pcdPin(0, self->y_g[luma_value] + (has_chroma ? (self->c1_g[chroma1_value] + self->c2_g[chroma2_value]) : 0), 1388);
	*bi = pcdPin(0, self->y_b[luma_value] + (has_chroma ? self->c1_b[chroma1_value] : 0), 1388);

	if ((self->colorSpace == kPCDLinearCCIR709ColorSpace) ||
		(self->colorSpace == kPCDsRGBColorSpace)) {
		*ri = toLinearLight[*ri];
		*gi = toLinearLight[*gi];
		*bi = toLinearLight[*bi];
		if (self->whiteBalance == kPCDD50White) {
			rt = *ri;
			gt = *gi;
			bt = *bi;
			*ri = (5930 * rt - 143 * gt + 393 * bt) >> 13;
			*gi = (-176 * rt + 6268 * gt + 131 * bt) >> 13;
			*bi = (76 * rt - 128 * gt + 8256 * bt) >> 13;
		}
	}

	if (self->colorSpace == kPCDsRGBColorSpace) {
		*ri = CCIR709tosRGB[pcdPin(0, *ri, 1388)];
		*gi = CCIR709tosRGB[pcdPin(0, *gi, 1388)];
		*bi = CCIR709tosRGB[pcdPin(0, *bi, 1388)];
	} else {
		*ri = pcdPin(0, *ri, 1388);
		*gi = pcdPin(0, *gi, 1388);
		*bi = pcdPin(0, *bi, 1388);
	}
}

static void photocdDecode_populateUInt8RowImpl(photocdDecode *self, uint8_t *rgb,
	unsigned int row, unsigned int column, unsigned int width)
{
	unsigned int x;
	unsigned int source_width = PCDLumaWidth[self->sceneNumber];
	unsigned int chroma_width = PCDChromaWidth[self->sceneNumber];
	unsigned int chroma_height = PCDChromaHeight[self->sceneNumber];
	bool has_chroma = !self->monochrome && (self->chroma1 != NULL) &&
		(self->chroma2 != NULL);

	for (x = 0; x < width; x++) {
		unsigned int src_x;
		unsigned int src_y;
		size_t luma_index;
		uint8_t c1_value = 0;
		uint8_t c2_value = 0;
		int32_t ri;
		int32_t gi;
		int32_t bi;

		photocdDecode_mapOutputToSource(self, column + x, row, &src_x, &src_y);
		luma_index = src_x + (size_t)src_y * source_width;
		if (has_chroma) {
			c1_value = photocdDecode_sampleChroma(self->chroma1, chroma_width,
				chroma_height, src_x, src_y, self->upResMethod);
			c2_value = photocdDecode_sampleChroma(self->chroma2, chroma_width,
				chroma_height, src_x, src_y, self->upResMethod);
		}

		photocdDecode_convertPixel(self, self->luma[luma_index], c1_value, c2_value,
			has_chroma, &ri, &gi, &bi);
		rgb[x * 3U + 0U] = uint8Output[ri];
		rgb[x * 3U + 1U] = uint8Output[gi];
		rgb[x * 3U + 2U] = uint8Output[bi];
	}
}

static bool photocdDecode_interpolateBuffers(photocdDecode *self, uint8_t **c1UpRes,
	uint8_t **c2UpRes, int *resFactor, const char **error);
static bool photocdDecode_populateBuffers(photocdDecode *self, void *red, void *green,
	void *blue, void *alpha, int d, int dataSize, const char **error);
static bool photocdDecode_postParseImpl(photocdDecode *self, const char **error);

static bool photocdDecode_interpolateBuffers(photocdDecode *self, uint8_t **c1UpRes,
	uint8_t **c2UpRes, int *resFactor, const char **error)
{
	// This does an interpolate either by a factor of 2 or 4
	uint8_t *lp, *c1p, *c2p, *intermediate;
	lp = self->luma;
	c1p = self->chroma1;
	c2p = self->chroma2;
	intermediate = NULL;	
#if defined(DEBUG)
	//	dumpColumn(lp, 356, PCDLumaHeight[self->sceneNumber], PCDLumaWidth[self->sceneNumber]);
	//	dump8by8(c1p, PCDChromaWidth[self->sceneNumber]);
#endif
	
	if (self->upResMethod >= kUpResIterpolate) {
		// Linear interpolation..........
		*c1UpRes = (uint8_t *) malloc(PCDLumaHeight[self->sceneNumber]*PCDLumaWidth[self->sceneNumber]*sizeof(uint8_t));
		*c2UpRes = (uint8_t *) malloc(PCDLumaHeight[self->sceneNumber]*PCDLumaWidth[self->sceneNumber]*sizeof(uint8_t));
		if (*c1UpRes == NULL || *c2UpRes == NULL) {
			free(*c1UpRes);
			free(*c2UpRes);
			*c1UpRes = NULL;
			*c2UpRes = NULL;
			return pcd_fail(error, "Memory Error!");
		}
		
		if (*resFactor == 2) {
			intermediate = (uint8_t *) malloc((PCDLumaHeight[self->sceneNumber]>>1)*(PCDLumaWidth[self->sceneNumber]>>1)*sizeof(uint8_t));
			if (intermediate == NULL) {
				free(*c1UpRes);
				free(*c2UpRes);
				*c1UpRes = NULL;
				*c2UpRes = NULL;
				return pcd_fail(error, "Memory Error!");
			}
			upResBuffer(c1p, intermediate, NULL, PCDLumaWidth[self->sceneNumber]>>1, PCDLumaHeight[self->sceneNumber]>>1, self->upResMethod, false);
			c1p = intermediate;
#if defined(DEBUG)
			dump8by8(c1p, PCDLumaWidth[self->sceneNumber]>>1);
#endif
		}

		upResBuffer(c1p, *c1UpRes, lp, PCDLumaWidth[self->sceneNumber], PCDLumaHeight[self->sceneNumber], self->upResMethod, false);
		c1p = *c1UpRes;
		
		if (*resFactor == 2) {
			upResBuffer(c2p, intermediate, NULL, PCDLumaWidth[self->sceneNumber]>>1, PCDLumaHeight[self->sceneNumber]>>1, self->upResMethod, false);
			c2p = intermediate;
		}
		upResBuffer(c2p, *c2UpRes, lp, PCDLumaWidth[self->sceneNumber], PCDLumaHeight[self->sceneNumber], self->upResMethod, false);
		c2p = *c2UpRes;

		free(intermediate);
		*resFactor = 0;
	}

	return true;
}

bool photocdDecode_populateFloatBuffers(photocdDecode *self, float *red, float *green,
	float *blue, float *alpha, int d)
{
	const char *error = NULL;

	if (self->pcdFileHeader == NULL) {
		pcd_set_error_string(self, "No image has been loaded");
		return false;
	}

	if (!photocdDecode_populateBuffers(self, red, green, blue, alpha, d, pcdFloatSize, &error)) {
		pcd_set_error_string(self,
			error != NULL ? error : "Could not convert image data");
		return false;
	}

	return true;
}

bool photocdDecode_populateUInt16Buffers(photocdDecode *self, uint16_t *red,
	uint16_t *green, uint16_t *blue, uint16_t *alpha, int d)
{
	const char *error = NULL;

	if (self->pcdFileHeader == NULL) {
		pcd_set_error_string(self, "No image has been loaded");
		return false;
	}

	if (!photocdDecode_populateBuffers(self, red, green, blue, alpha, d, pcdInt16Size, &error)) {
		pcd_set_error_string(self,
			error != NULL ? error : "Could not convert image data");
		return false;
	}

	return true;
}

bool photocdDecode_populateUInt8Buffers(photocdDecode *self, uint8_t *red,
	uint8_t *green, uint8_t *blue, uint8_t *alpha, int d)
{
	const char *error = NULL;

	if (self->pcdFileHeader == NULL) {
		pcd_set_error_string(self, "No image has been loaded");
		return false;
	}

	if (!photocdDecode_populateBuffers(self, red, green, blue, alpha, d, pcdByteSize, &error)) {
		pcd_set_error_string(self,
			error != NULL ? error : "Could not convert image data");
		return false;
	}

	return true;
}

bool photocdDecode_populateUInt8Row(photocdDecode *self, uint8_t *rgb, unsigned int row,
	unsigned int column, unsigned int width)
{
	unsigned int image_width;
	unsigned int image_height;

	if (self->pcdFileHeader == NULL) {
		pcd_set_error_string(self, "No image has been loaded");
		return false;
	}

	image_width = photocdDecode_getWidth(self);
	image_height = photocdDecode_getHeight(self);
	if (row >= image_height || column > image_width || width > image_width - column) {
		pcd_set_error_string(self, "Requested output row is out of range");
		return false;
	}

	photocdDecode_populateUInt8RowImpl(self, rgb, row, column, width);
	return true;
}

static bool photocdDecode_populateBuffers(photocdDecode *self, void *red, void *green,
	void *blue, void *alpha, int d, int dataSize, const char **error)
{

	uint8_t *lp, *c1p, *c2p, *c1UpRes, *c2UpRes;
	lp = self->luma;
	c1p = self->chroma1;
	c2p = self->chroma2;
	c1UpRes = NULL;
	c2UpRes = NULL;
	int resFactor = PCDChromaResFactor[self->sceneNumber];
	
	if (self->pcdFileHeader == NULL) {
		// No file
		return pcd_fail(error, "No image has been loaded");
	}

	photocdDecode_prepareLUTs(self);
	
#if defined(DEBUG)
//	dumpColumn(lp, 356, PCDLumaHeight[self->sceneNumber], PCDLumaWidth[self->sceneNumber]);
//	dump8by8(c1p, PCDChromaWidth[self->sceneNumber]);
#endif
	
	if (!photocdDecode_interpolateBuffers(self, &c1UpRes, &c2UpRes, &resFactor, error)) {
		return false;
	}
	if (c1UpRes != NULL) c1p = c1UpRes;
	if (c2UpRes != NULL) c2p = c2UpRes;

#if defined(DEBUG)
//	dump8by8(c1p, PCDLumaWidth[self->sceneNumber]);
#endif	
	struct ConvertToRGBData rd[kNumThreads];
	size_t previousRow = 0;	
	int thread;
#ifndef mNoPThreads
	int rc;
	void *status;
	pcdThreadDescriptor threadDescriptors[kNumThreads];
	pthread_attr_t threadAttr;
	
	/* Initialize and set thread detached attribute */
	pthread_attr_init(&threadAttr);
	pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_JOINABLE);
	// Use minimum stacksize times two; we have only a few stack variables
	pthread_attr_setstacksize(&threadAttr, PTHREAD_STACK_MIN<<1);
#endif

	for (thread = 0; thread < kNumThreads; thread++) {
		rd[thread].outputSize = dataSize;
		rd[thread].red = red;
		rd[thread].green = green;
		rd[thread].blue = blue;
		rd[thread].alpha = alpha;
		rd[thread].d = d;
		rd[thread].startRow = previousRow;
		rd[thread].endRow = PCDLumaHeight[self->sceneNumber]/kNumThreads*(thread+1);
		rd[thread].columns = PCDLumaWidth[self->sceneNumber];
		rd[thread].rows = PCDLumaHeight[self->sceneNumber];
		rd[thread].lp = lp;
		rd[thread].c1p = self->monochrome ? NULL : c1p;
		rd[thread].c2p = self->monochrome ? NULL : c2p;
		rd[thread].resFactor = resFactor;
		rd[thread].imageRotate = self->imageRotate;
		rd[thread].colorSpace = self->colorSpace;		
		rd[thread].whiteBalance = self->whiteBalance;		
		
		previousRow = rd[thread].endRow;
#ifndef mNoPThreads
		if (thread == (kNumThreads - 1)) {
			convertToRGB(&(rd[thread]));
		}
		else {
			if (pcdStartThread(threadDescriptors[thread], threadAttr, convertToRGB, (void *)&(rd[thread])) != 0) {
				// Too many threads already.....
				convertToRGB(&(rd[thread]));
				// Don't try to join
				rd[thread].endRow = 0;
			}
		}
#else
		convertToRGB(&(rd[thread]));
#endif
	}

#ifndef mNoPThreads
	pthread_attr_destroy(&threadAttr);
	status  = 0; // Avoid unreferenced local variable warning
	for (thread = 0; thread < (kNumThreads-1); thread++) {
		if (rd[thread].endRow > 0) {
			rc = pcdThreadJoin(threadDescriptors[thread], &status);
		}
	}
#endif

	if (c1UpRes != NULL) {
		free(c1UpRes);
		c1UpRes = NULL;
	}
	if (c2UpRes != NULL) {
		free(c2UpRes);
		c2UpRes = NULL;
	}

	return true;
}


int photocdDecode_getOrientation(photocdDecode *self)
{	
	return self->imageRotate;
}

void photocdDecode_setOrientation(photocdDecode *self, unsigned int value)
{
	self->imageRotate = value & 3U;
}

bool photocdDecode_postParse(photocdDecode *self)
{
	const char *error = NULL;

	if (self->pcdFileHeader == NULL) {
		pcd_set_error_string(self, "No image has been loaded");
		return false;
	}

	if (!photocdDecode_postParseImpl(self, &error)) {
		pcd_set_error_string(self,
			error != NULL ? error : "Could not prepare image data");
		return false;
	}

	return true;
}

static bool photocdDecode_postParseImpl(photocdDecode *self, const char **error)
{
	int scene_index;
	bool haveDeltas;

	photocdDecode_prepareLUTs(self);
	
	for (scene_index = k4Base; scene_index <= k64Base; scene_index++) {
		// Iterate the possible self->deltas that are avalable......
		if (self->deltas[scene_index-k4Base][0] != NULL) {
			// First the self->luma delta....
			upResBuffer(self->luma, self->deltas[scene_index-k4Base][0], NULL, PCDLumaWidth[scene_index], PCDLumaHeight[scene_index], pcdMin(kUpResIterpolate, self->upResMethod), true);
			if (self->deltas[scene_index-k4Base][0] != NULL) {
				free(self->luma);
				self->luma = self->deltas[scene_index-k4Base][0];
				self->deltas[scene_index-k4Base][0] = NULL;
			}
			// If there is a self->luma delta, we have to upres the chromas as well.....
			haveDeltas = (self->deltas[scene_index-k4Base][1] != NULL);
			if (!haveDeltas) {
				self->deltas[scene_index-k4Base][1] = (uint8_t *) malloc((PCDLumaWidth[scene_index]>>1) * (PCDLumaHeight[scene_index]>>1)*sizeof(uint8_t));
				if (self->deltas[scene_index-k4Base][1] == NULL) {
					return pcd_fail(error, "Memory Error!");
				}
			}
			upResBuffer(self->chroma1, self->deltas[scene_index-k4Base][1], NULL, PCDLumaWidth[scene_index]>>1, PCDLumaHeight[scene_index]>>1, pcdMin(kUpResIterpolate, self->upResMethod), haveDeltas);
			if (self->deltas[scene_index-k4Base][1] != NULL) {
				free(self->chroma1);
				self->chroma1 = self->deltas[scene_index-k4Base][1];
				self->deltas[scene_index-k4Base][1] = NULL;
			}
			haveDeltas = (self->deltas[scene_index-k4Base][2] != NULL);
			if (!haveDeltas) {
				self->deltas[scene_index-k4Base][2] = (uint8_t *) malloc((PCDLumaWidth[scene_index]>>1) * (PCDLumaHeight[scene_index]>>1)*sizeof(uint8_t));
				if (self->deltas[scene_index-k4Base][2] == NULL) {
					return pcd_fail(error, "Memory Error!");
				}
			}
			upResBuffer(self->chroma2, self->deltas[scene_index-k4Base][2], NULL, PCDLumaWidth[scene_index]>>1, PCDLumaHeight[scene_index]>>1, pcdMin(kUpResIterpolate, self->upResMethod), haveDeltas);
			if (self->deltas[scene_index-k4Base][2] != NULL) {
				free(self->chroma2);
				self->chroma2 = self->deltas[scene_index-k4Base][2];
				self->deltas[scene_index-k4Base][2] = NULL;
			}
		}
	}

	return true;
}
