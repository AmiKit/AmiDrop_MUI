/**
 * Compact AmiDrop build of ricmoo/QRCode, fixed to QR version 3.
 *
 * The MIT License (MIT)
 * Copyright (c) 2017 Richard Moore (https://github.com/ricmoo/QRCode)
 * Copyright (c) 2017 Project Nayuki (https://www.nayuki.io/page/qr-code-generator-library)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qrcode.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Version 3 only. Array order is QR format-bit order: M, L, H, Q. */
static const int16_t NUM_ERROR_CORRECTION_CODEWORDS[4] = { 26, 15, 44, 36 };
static const int8_t NUM_ERROR_CORRECTION_BLOCKS[4] = { 1, 1, 2, 2 };
static const uint16_t NUM_RAW_DATA_MODULES = 567;

static int qr_max(int a, int b)
{
    return a > b ? a : b;
}

static int8_t getAlphanumeric(char c)
{
    if (c >= '0' && c <= '9') return (int8_t)(c - '0');
    if (c >= 'A' && c <= 'Z') return (int8_t)(c - 'A' + 10);
    switch (c) {
        case ' ': return 36;
        case '$': return 37;
        case '%': return 38;
        case '*': return 39;
        case '+': return 40;
        case '-': return 41;
        case '.': return 42;
        case '/': return 43;
        case ':': return 44;
        default: return -1;
    }
}

static qr_bool isAlphanumeric(const char *text, uint16_t length)
{
    while (length != 0) {
        if (getAlphanumeric(text[--length]) == -1) return 0;
    }
    return 1;
}

static qr_bool isNumeric(const char *text, uint16_t length)
{
    while (length != 0) {
        char c = text[--length];
        if (c < '0' || c > '9') return 0;
    }
    return 1;
}

static char getModeBits(uint8_t version, uint8_t mode)
{
    unsigned int modeInfo = 0x7bbb80a;
    char result;

    if (version > 9) modeInfo >>= 9;
    if (version > 26) modeInfo >>= 9;
    result = (char)(8 + ((modeInfo >> (3 * mode)) & 0x07));
    if (result == 15) result = 16;
    return result;
}

typedef struct BitBucket {
    uint32_t bitOffsetOrWidth;
    uint16_t capacityBytes;
    uint8_t *data;
} BitBucket;

static uint16_t bb_getGridSizeBytes(uint8_t size)
{
    return (uint16_t)(((uint16_t)size * (uint16_t)size + 7U) / 8U);
}

static uint16_t bb_getBufferSizeBytes(uint32_t bits)
{
    return (uint16_t)((bits + 7U) / 8U);
}

static void bb_initBuffer(BitBucket *bitBuffer, uint8_t *data, int32_t capacityBytes)
{
    bitBuffer->bitOffsetOrWidth = 0;
    bitBuffer->capacityBytes = (uint16_t)capacityBytes;
    bitBuffer->data = data;
    memset(data, 0, bitBuffer->capacityBytes);
}

static void bb_initGrid(BitBucket *bitGrid, uint8_t *data, uint8_t size)
{
    bitGrid->bitOffsetOrWidth = size;
    bitGrid->capacityBytes = bb_getGridSizeBytes(size);
    bitGrid->data = data;
    memset(data, 0, bitGrid->capacityBytes);
}

static void bb_appendBits(BitBucket *bitBuffer, uint32_t val, uint8_t length)
{
    uint32_t offset = bitBuffer->bitOffsetOrWidth;
    int8_t i;

    for (i = (int8_t)length - 1; i >= 0; --i, ++offset) {
        bitBuffer->data[offset >> 3] |= (uint8_t)(((val >> i) & 1U) << (7 - (offset & 7U)));
    }
    bitBuffer->bitOffsetOrWidth = offset;
}

static void bb_setBit(BitBucket *bitGrid, uint8_t x, uint8_t y, qr_bool on)
{
    uint32_t offset = (uint32_t)y * bitGrid->bitOffsetOrWidth + x;
    uint8_t mask = (uint8_t)(1U << (7 - (offset & 7U)));
    if (on) bitGrid->data[offset >> 3] |= mask;
    else bitGrid->data[offset >> 3] &= (uint8_t)~mask;
}

static void bb_invertBit(BitBucket *bitGrid, uint8_t x, uint8_t y, qr_bool invert)
{
    uint32_t offset = (uint32_t)y * bitGrid->bitOffsetOrWidth + x;
    uint8_t mask = (uint8_t)(1U << (7 - (offset & 7U)));
    qr_bool on = (bitGrid->data[offset >> 3] & mask) != 0;
    if (on ^ invert) bitGrid->data[offset >> 3] |= mask;
    else bitGrid->data[offset >> 3] &= (uint8_t)~mask;
}

static qr_bool bb_getBit(BitBucket *bitGrid, uint8_t x, uint8_t y)
{
    uint32_t offset = (uint32_t)y * bitGrid->bitOffsetOrWidth + x;
    return (bitGrid->data[offset >> 3] & (1U << (7 - (offset & 7U)))) != 0;
}

static void applyMask(BitBucket *modules, BitBucket *isFunction, uint8_t mask)
{
    uint8_t size = (uint8_t)modules->bitOffsetOrWidth;
    uint8_t y;

    for (y = 0; y < size; ++y) {
        uint8_t x;
        for (x = 0; x < size; ++x) {
            qr_bool invert = 0;
            if (bb_getBit(isFunction, x, y)) continue;
            switch (mask) {
                case 0: invert = ((x + y) % 2) == 0; break;
                case 1: invert = (y % 2) == 0; break;
                case 2: invert = (x % 3) == 0; break;
                case 3: invert = ((x + y) % 3) == 0; break;
                case 4: invert = ((x / 3 + y / 2) % 2) == 0; break;
                case 5: invert = ((x * y) % 2 + (x * y) % 3) == 0; break;
                case 6: invert = (((x * y) % 2 + (x * y) % 3) % 2) == 0; break;
                case 7: invert = ((((x + y) % 2) + (x * y) % 3) % 2) == 0; break;
                default: invert = 0; break;
            }
            bb_invertBit(modules, x, y, invert);
        }
    }
}

static void setFunctionModule(BitBucket *modules, BitBucket *isFunction,
                              uint8_t x, uint8_t y, qr_bool on)
{
    bb_setBit(modules, x, y, on);
    bb_setBit(isFunction, x, y, 1);
}

static void drawFinderPattern(BitBucket *modules, BitBucket *isFunction, uint8_t x, uint8_t y)
{
    uint8_t size = (uint8_t)modules->bitOffsetOrWidth;
    int8_t i;

    for (i = -4; i <= 4; ++i) {
        int8_t j;
        for (j = -4; j <= 4; ++j) {
            uint8_t dist = (uint8_t)qr_max(abs(i), abs(j));
            int16_t xx = (int16_t)x + j;
            int16_t yy = (int16_t)y + i;
            if (xx >= 0 && xx < size && yy >= 0 && yy < size) {
                setFunctionModule(modules, isFunction, (uint8_t)xx, (uint8_t)yy,
                                  dist != 2 && dist != 4);
            }
        }
    }
}

static void drawAlignmentPattern(BitBucket *modules, BitBucket *isFunction, uint8_t x, uint8_t y)
{
    int8_t i;
    for (i = -2; i <= 2; ++i) {
        int8_t j;
        for (j = -2; j <= 2; ++j) {
            setFunctionModule(modules, isFunction, (uint8_t)(x + j), (uint8_t)(y + i),
                              qr_max(abs(i), abs(j)) != 1);
        }
    }
}

static void drawFormatBits(BitBucket *modules, BitBucket *isFunction, uint8_t ecc, uint8_t mask)
{
    uint8_t size = (uint8_t)modules->bitOffsetOrWidth;
    uint32_t data = ((uint32_t)ecc << 3) | mask;
    uint32_t rem = data;
    uint8_t i;

    for (i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537U);
    data = (data << 10) | rem;
    data ^= 0x5412U;

    for (i = 0; i <= 5; ++i)
        setFunctionModule(modules, isFunction, 8, i, ((data >> i) & 1U) != 0);
    setFunctionModule(modules, isFunction, 8, 7, ((data >> 6) & 1U) != 0);
    setFunctionModule(modules, isFunction, 8, 8, ((data >> 7) & 1U) != 0);
    setFunctionModule(modules, isFunction, 7, 8, ((data >> 8) & 1U) != 0);
    for (i = 9; i < 15; ++i)
        setFunctionModule(modules, isFunction, (uint8_t)(14 - i), 8, ((data >> i) & 1U) != 0);
    for (i = 0; i <= 7; ++i)
        setFunctionModule(modules, isFunction, (uint8_t)(size - 1 - i), 8, ((data >> i) & 1U) != 0);
    for (i = 8; i < 15; ++i)
        setFunctionModule(modules, isFunction, 8, (uint8_t)(size - 15 + i), ((data >> i) & 1U) != 0);
    setFunctionModule(modules, isFunction, 8, (uint8_t)(size - 8), 1);
}

static void drawFunctionPatterns(BitBucket *modules, BitBucket *isFunction, uint8_t version, uint8_t ecc)
{
    uint8_t size = (uint8_t)modules->bitOffsetOrWidth;
    uint8_t i;

    for (i = 0; i < size; ++i) {
        setFunctionModule(modules, isFunction, 6, i, (i % 2) == 0);
        setFunctionModule(modules, isFunction, i, 6, (i % 2) == 0);
    }

    drawFinderPattern(modules, isFunction, 3, 3);
    drawFinderPattern(modules, isFunction, (uint8_t)(size - 4), 3);
    drawFinderPattern(modules, isFunction, 3, (uint8_t)(size - 4));

    if (version > 1) {
        uint8_t alignCount = (uint8_t)(version / 7 + 2);
        uint8_t step;
        uint8_t alignPositionIndex = (uint8_t)(alignCount - 1);
        uint8_t alignPosition[7];
        uint8_t pos;

        if (version != 32)
            step = (uint8_t)(((version * 4 + alignCount * 2 + 1) / (2 * alignCount - 2)) * 2);
        else
            step = 26;

        alignPosition[0] = 6;
        pos = (uint8_t)(version * 4 + 17 - 7);
        for (i = 0; i < alignCount - 1; ++i, pos = (uint8_t)(pos - step))
            alignPosition[alignPositionIndex--] = pos;

        for (i = 0; i < alignCount; ++i) {
            uint8_t j;
            for (j = 0; j < alignCount; ++j) {
                if ((i == 0 && j == 0) ||
                    (i == 0 && j == alignCount - 1) ||
                    (i == alignCount - 1 && j == 0)) continue;
                drawAlignmentPattern(modules, isFunction, alignPosition[i], alignPosition[j]);
            }
        }
    }

    drawFormatBits(modules, isFunction, ecc, 0);
}

static void drawCodewords(BitBucket *modules, BitBucket *isFunction, BitBucket *codewords)
{
    uint32_t bitLength = codewords->bitOffsetOrWidth;
    uint8_t *data = codewords->data;
    uint8_t size = (uint8_t)modules->bitOffsetOrWidth;
    uint32_t i = 0;
    int16_t right;

    for (right = (int16_t)size - 1; right >= 1; right -= 2) {
        uint8_t vert;
        if (right == 6) right = 5;
        for (vert = 0; vert < size; ++vert) {
            int j;
            for (j = 0; j < 2; ++j) {
                uint8_t x = (uint8_t)(right - j);
                qr_bool upwards = (((right & 2) == 0) ^ (x < 6));
                uint8_t y = upwards ? (uint8_t)(size - 1 - vert) : vert;
                if (!bb_getBit(isFunction, x, y) && i < bitLength) {
                    bb_setBit(modules, x, y,
                              ((data[i >> 3] >> (7 - (i & 7U))) & 1U) != 0);
                    ++i;
                }
            }
        }
    }
}

#define PENALTY_N1 3
#define PENALTY_N2 3
#define PENALTY_N3 40
#define PENALTY_N4 10

static uint32_t getPenaltyScore(BitBucket *modules)
{
    uint32_t result = 0;
    uint8_t size = (uint8_t)modules->bitOffsetOrWidth;
    uint8_t y;
    uint16_t black = 0;

    for (y = 0; y < size; ++y) {
        qr_bool colorX = bb_getBit(modules, 0, y);
        uint8_t runX = 1;
        uint8_t x;
        for (x = 1; x < size; ++x) {
            qr_bool cx = bb_getBit(modules, x, y);
            if (cx != colorX) {
                colorX = cx;
                runX = 1;
            } else {
                ++runX;
                if (runX == 5) result += PENALTY_N1;
                else if (runX > 5) ++result;
            }
        }
    }

    {
        uint8_t x;
        for (x = 0; x < size; ++x) {
            qr_bool colorY = bb_getBit(modules, x, 0);
            uint8_t runY = 1;
            for (y = 1; y < size; ++y) {
                qr_bool cy = bb_getBit(modules, x, y);
                if (cy != colorY) {
                    colorY = cy;
                    runY = 1;
                } else {
                    ++runY;
                    if (runY == 5) result += PENALTY_N1;
                    else if (runY > 5) ++result;
                }
            }
        }
    }

    for (y = 0; y < size; ++y) {
        uint16_t bitsRow = 0;
        uint16_t bitsCol = 0;
        uint8_t x;
        for (x = 0; x < size; ++x) {
            qr_bool color = bb_getBit(modules, x, y);
            if (x > 0 && y > 0) {
                qr_bool colorUL = bb_getBit(modules, (uint8_t)(x - 1), (uint8_t)(y - 1));
                qr_bool colorUR = bb_getBit(modules, x, (uint8_t)(y - 1));
                qr_bool colorL = bb_getBit(modules, (uint8_t)(x - 1), y);
                if (color == colorUL && color == colorUR && color == colorL) result += PENALTY_N2;
            }

            bitsRow = (uint16_t)(((bitsRow << 1) & 0x7FFU) | (color ? 1U : 0U));
            bitsCol = (uint16_t)(((bitsCol << 1) & 0x7FFU) |
                                 (bb_getBit(modules, y, x) ? 1U : 0U));
            if (x >= 10) {
                if (bitsRow == 0x05D || bitsRow == 0x5D0) result += PENALTY_N3;
                if (bitsCol == 0x05D || bitsCol == 0x5D0) result += PENALTY_N3;
            }
            if (color) ++black;
        }
    }

    {
        uint16_t total = (uint16_t)(size * size);
        uint16_t k = 0;
        while ((uint32_t)black * 20U < (uint32_t)(9 - k) * total ||
               (uint32_t)black * 20U > (uint32_t)(11 + k) * total) {
            result += PENALTY_N4;
            ++k;
        }
    }
    return result;
}

static uint8_t rs_multiply(uint8_t x, uint8_t y)
{
    uint16_t z = 0;
    int8_t i;
    for (i = 7; i >= 0; --i) {
        z = (uint16_t)((z << 1) ^ ((z >> 7) * 0x11DU));
        z ^= (uint16_t)(((y >> i) & 1U) * x);
    }
    return (uint8_t)z;
}

static void rs_init(uint8_t degree, uint8_t *coeff)
{
    uint16_t root = 1;
    uint8_t i;
    memset(coeff, 0, degree);
    coeff[degree - 1] = 1;

    for (i = 0; i < degree; ++i) {
        uint8_t j;
        for (j = 0; j < degree; ++j) {
            coeff[j] = rs_multiply(coeff[j], (uint8_t)root);
            if (j + 1 < degree) coeff[j] ^= coeff[j + 1];
        }
        root = (uint16_t)((root << 1) ^ ((root >> 7) * 0x11DU));
    }
}

static void rs_getRemainder(uint8_t degree, uint8_t *coeff, uint8_t *data,
                            uint8_t length, uint8_t *result, uint8_t stride)
{
    uint8_t i;
    for (i = 0; i < length; ++i) {
        uint8_t factor = data[i] ^ result[0];
        uint8_t j;
        for (j = 1; j < degree; ++j) result[(j - 1) * stride] = result[j * stride];
        result[(degree - 1) * stride] = 0;
        for (j = 0; j < degree; ++j) result[j * stride] ^= rs_multiply(coeff[j], factor);
    }
}

static int8_t encodeDataCodewords(BitBucket *dataCodewords, const uint8_t *text,
                                  uint16_t length, uint8_t version)
{
    int8_t mode = MODE_BYTE;

    if (isNumeric((const char *)text, length)) {
        uint16_t accumData = 0;
        uint8_t accumCount = 0;
        uint16_t i;
        mode = MODE_NUMERIC;
        bb_appendBits(dataCodewords, 1U << MODE_NUMERIC, 4);
        bb_appendBits(dataCodewords, length, (uint8_t)getModeBits(version, MODE_NUMERIC));
        for (i = 0; i < length; ++i) {
            accumData = (uint16_t)(accumData * 10 + (text[i] - '0'));
            if (++accumCount == 3) {
                bb_appendBits(dataCodewords, accumData, 10);
                accumData = 0;
                accumCount = 0;
            }
        }
        if (accumCount > 0) bb_appendBits(dataCodewords, accumData, (uint8_t)(accumCount * 3 + 1));
    } else if (isAlphanumeric((const char *)text, length)) {
        uint16_t accumData = 0;
        uint8_t accumCount = 0;
        uint16_t i;
        mode = MODE_ALPHANUMERIC;
        bb_appendBits(dataCodewords, 1U << MODE_ALPHANUMERIC, 4);
        bb_appendBits(dataCodewords, length, (uint8_t)getModeBits(version, MODE_ALPHANUMERIC));
        for (i = 0; i < length; ++i) {
            accumData = (uint16_t)(accumData * 45 + getAlphanumeric((char)text[i]));
            if (++accumCount == 2) {
                bb_appendBits(dataCodewords, accumData, 11);
                accumData = 0;
                accumCount = 0;
            }
        }
        if (accumCount > 0) bb_appendBits(dataCodewords, accumData, 6);
    } else {
        uint16_t i;
        bb_appendBits(dataCodewords, 1U << MODE_BYTE, 4);
        bb_appendBits(dataCodewords, length, (uint8_t)getModeBits(version, MODE_BYTE));
        for (i = 0; i < length; ++i) bb_appendBits(dataCodewords, text[i], 8);
    }
    return mode;
}

static void performErrorCorrection(uint8_t ecc, BitBucket *data)
{
    uint8_t numBlocks = (uint8_t)NUM_ERROR_CORRECTION_BLOCKS[ecc];
    uint16_t totalEcc = (uint16_t)NUM_ERROR_CORRECTION_CODEWORDS[ecc];
    uint16_t moduleCount = NUM_RAW_DATA_MODULES;
    uint8_t blockEccLen = (uint8_t)(totalEcc / numBlocks);
    uint8_t numShortBlocks = (uint8_t)(numBlocks - moduleCount / 8 % numBlocks);
    uint8_t shortBlockLen = (uint8_t)(moduleCount / 8 / numBlocks);
    uint8_t shortDataBlockLen = (uint8_t)(shortBlockLen - blockEccLen);
    uint8_t result[72];
    uint8_t coeff[44];
    uint16_t offset = 0;
    uint8_t *dataBytes = data->data;
    uint8_t i;

    memset(result, 0, sizeof(result));
    rs_init(blockEccLen, coeff);

    for (i = 0; i < shortDataBlockLen; ++i) {
        uint16_t index = i;
        uint8_t stride = shortDataBlockLen;
        uint8_t blockNum;
        for (blockNum = 0; blockNum < numBlocks; ++blockNum) {
            result[offset++] = dataBytes[index];
            index += stride;
        }
    }

    {
        uint8_t blockSize = shortDataBlockLen;
        uint8_t blockNum;
        for (blockNum = 0; blockNum < numBlocks; ++blockNum) {
            rs_getRemainder(blockEccLen, coeff, dataBytes, blockSize,
                            &result[offset + blockNum], numBlocks);
            dataBytes += blockSize;
        }
    }

    memcpy(data->data, result, data->capacityBytes);
    data->bitOffsetOrWidth = moduleCount;
    (void)numShortBlocks;
}

static const uint8_t ECC_FORMAT_BITS = (0x02U << 6) | (0x03U << 4) | (0x00U << 2) | 0x01U;

uint16_t qrcode_getBufferSize(uint8_t version)
{
    return bb_getGridSizeBytes((uint8_t)(4 * version + 17));
}

int8_t qrcode_initBytes(QRCode *qrcode, uint8_t *modules, uint8_t version,
                        uint8_t ecc, uint8_t *data, uint16_t length)
{
    uint8_t size;
    uint8_t eccFormatBits;
    uint16_t moduleCount = NUM_RAW_DATA_MODULES;
    uint16_t dataCapacity;
    BitBucket codewords;
    uint8_t codewordBytes[72];
    int8_t mode;
    uint32_t requiredBits;
    uint32_t padding;
    BitBucket modulesGrid;
    BitBucket isFunctionGrid;
    uint8_t isFunctionGridBytes[106];
    uint8_t mask = 0;
    int32_t minPenalty = INT32_MAX;
    uint8_t i;

    if (!qrcode || !modules || !data || version != LOCK_VERSION || ecc > ECC_HIGH) return -1;
    size = (uint8_t)(version * 4 + 17);
    qrcode->version = version;
    qrcode->size = size;
    qrcode->ecc = ecc;
    qrcode->modules = modules;

    eccFormatBits = (uint8_t)((ECC_FORMAT_BITS >> (2 * ecc)) & 0x03U);
    dataCapacity = (uint16_t)(moduleCount / 8 - NUM_ERROR_CORRECTION_CODEWORDS[eccFormatBits]);

    /* Prevent the original library's unsigned padding underflow on oversized input. */
    requiredBits = 4U + (uint32_t)getModeBits(version, MODE_BYTE) + (uint32_t)length * 8U;
    if (requiredBits > (uint32_t)dataCapacity * 8U) return -2;

    bb_initBuffer(&codewords, codewordBytes, (int32_t)bb_getBufferSizeBytes(moduleCount));
    mode = encodeDataCodewords(&codewords, data, length, version);
    if (mode < 0 || codewords.bitOffsetOrWidth > (uint32_t)dataCapacity * 8U) return -2;
    qrcode->mode = (uint8_t)mode;

    padding = (uint32_t)dataCapacity * 8U - codewords.bitOffsetOrWidth;
    if (padding > 4) padding = 4;
    bb_appendBits(&codewords, 0, (uint8_t)padding);
    bb_appendBits(&codewords, 0, (uint8_t)((8 - codewords.bitOffsetOrWidth % 8U) % 8U));
    {
        uint8_t padByte = 0xEC;
        while (codewords.bitOffsetOrWidth < (uint32_t)dataCapacity * 8U) {
            bb_appendBits(&codewords, padByte, 8);
            padByte ^= (uint8_t)(0xECU ^ 0x11U);
        }
    }

    bb_initGrid(&modulesGrid, modules, size);
    bb_initGrid(&isFunctionGrid, isFunctionGridBytes, size);
    drawFunctionPatterns(&modulesGrid, &isFunctionGrid, version, eccFormatBits);
    performErrorCorrection(eccFormatBits, &codewords);
    drawCodewords(&modulesGrid, &isFunctionGrid, &codewords);

    for (i = 0; i < 8; ++i) {
        int32_t penalty;
        drawFormatBits(&modulesGrid, &isFunctionGrid, eccFormatBits, i);
        applyMask(&modulesGrid, &isFunctionGrid, i);
        penalty = (int32_t)getPenaltyScore(&modulesGrid);
        if (penalty < minPenalty) {
            mask = i;
            minPenalty = penalty;
        }
        applyMask(&modulesGrid, &isFunctionGrid, i);
    }

    qrcode->mask = mask;
    drawFormatBits(&modulesGrid, &isFunctionGrid, eccFormatBits, mask);
    applyMask(&modulesGrid, &isFunctionGrid, mask);
    return 0;
}

int8_t qrcode_initText(QRCode *qrcode, uint8_t *modules, uint8_t version,
                       uint8_t ecc, const char *data)
{
    size_t length;
    if (!data) return -1;
    length = strlen(data);
    if (length > 65535U) return -2;
    return qrcode_initBytes(qrcode, modules, version, ecc, (uint8_t *)data, (uint16_t)length);
}

qr_bool qrcode_getModule(QRCode *qrcode, uint8_t x, uint8_t y)
{
    uint32_t offset;
    if (!qrcode || x >= qrcode->size || y >= qrcode->size) return 0;
    offset = (uint32_t)y * qrcode->size + x;
    return (qrcode->modules[offset >> 3] & (1U << (7 - (offset & 7U)))) != 0;
}
