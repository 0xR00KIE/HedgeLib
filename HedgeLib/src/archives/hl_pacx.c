#include "hedgelib/archives/hl_pacx.h"
#include "hedgelib/io/hl_path.h"
#include "hedgelib/hl_endian.h"
#include "hedgelib/hl_list.h"
#include "../io/hl_in_path.h"
#include "../hl_in_assert.h"
#include "../depends/lz4/lz4.h"

static const char* const HlINPACxV2SplitType = "ResPacDepend";

void hlPACxV2NodeSwap(HlPACxV2Node* node, HlBool swapOffsets)
{
    /*
       There are no offsets in PACxV2 nodes - we only provide the
       swapOffsets boolean for consistency with other Swap functions.
    */
    if (!swapOffsets) return;

    hlSwapU32P(&node->name);
    hlSwapU32P(&node->data);
}

void hlPACxV2NodeTreeSwap(HlPACxV2NodeTree* nodeTree, HlBool swapOffsets)
{
    hlSwapU32P(&nodeTree->nodeCount);
    if (swapOffsets) hlSwapU32P(&nodeTree->nodes);
}

void hlPACxV2DataEntrySwap(HlPACxV2DataEntry* dataEntry)
{
    hlSwapU32P(&dataEntry->dataSize);
    hlSwapU32P(&dataEntry->unknown1);
    hlSwapU32P(&dataEntry->unknown2);
}

void hlPACxV2SplitTableSwap(HlPACxV2SplitTable* splitTable, HlBool swapOffsets)
{
    if (swapOffsets) hlSwapU32P(&splitTable->splitNames);
    hlSwapU32P(&splitTable->splitCount);
}

void hlPACxV2ProxyEntrySwap(HlPACxV2ProxyEntry* proxyEntry, HlBool swapOffsets)
{
    if (swapOffsets)
    {
        hlSwapU32P(&proxyEntry->type);
        hlSwapU32P(&proxyEntry->name);
    }

    hlSwapU32P(&proxyEntry->nodeIndex);
}

void hlPACxV2ProxyEntryTableSwap(HlPACxV2ProxyEntryTable* proxyEntryTable,
    HlBool swapOffsets)
{
    hlSwapU32P(&proxyEntryTable->proxyEntryCount);
    if (swapOffsets) hlSwapU32P(&proxyEntryTable->proxyEntries);
}

void hlPACxV2DataHeaderSwap(HlPACxV2BlockDataHeader* dataHeader)
{
    hlSwapU32P(&dataHeader->size);
    hlSwapU32P(&dataHeader->dataEntriesSize);
    hlSwapU32P(&dataHeader->treesSize);
    hlSwapU32P(&dataHeader->proxyTableSize);
    hlSwapU32P(&dataHeader->stringTableSize);
    hlSwapU32P(&dataHeader->offsetTableSize);
}

static void hlINPACxV2DataBlockSwapRecursive(HlPACxV2BlockDataHeader* dataBlock)
{
    /* Swap trees, data entries, and split tables. */
    {
        /* Get type tree and type nodes pointer (safe because we already swapped offsets). */
        HlPACxV2NodeTree* typeTree = hlPACxV2DataGetTypeTree(dataBlock);
        HlPACxV2Node* typeNodes = (HlPACxV2Node*)hlOff32Get(&typeTree->nodes);
        HlU32 i;

        /* Swap type tree. */
        hlPACxV2NodeTreeSwap(typeTree, HL_FALSE);

        /* Swap file trees. */
        for (i = 0; i < typeTree->nodeCount; ++i)
        {
            /* Get pointers. */
            const char* typeStr = (const char*)hlOff32Get(&typeNodes[i].name);
            HlPACxV2NodeTree* fileTree = (HlPACxV2NodeTree*)hlOff32Get(
                &typeNodes[i].data);

            HlPACxV2Node* fileNodes = (HlPACxV2Node*)hlOff32Get(&fileTree->nodes);
            HlU32 i2;

            /* Swap file tree. */
            hlPACxV2NodeTreeSwap(fileTree, HL_FALSE);

            /* Swap data entries. */
            if (strcmp(strchr(typeStr, ':') + 1, HlINPACxV2SplitType))
            {
                for (i2 = 0; i2 < fileTree->nodeCount; ++i2)
                {
                    HlPACxV2DataEntry* dataEntry = (HlPACxV2DataEntry*)
                        hlOff32Get(&fileNodes[i2].data);

                    hlPACxV2DataEntrySwap(dataEntry);
                }
            }

            /* Swap split tables. */
            else
            {
                for (i2 = 0; i2 < fileTree->nodeCount; ++i2)
                {
                    HlPACxV2SplitTable* splitTable = (HlPACxV2SplitTable*)(
                        ((HlPACxV2DataEntry*)hlOff32Get(&fileNodes[i2].data)) + 1);

                    hlPACxV2SplitTableSwap(splitTable, HL_FALSE);
                }
            }
        }
    }

    /* Swap proxy entry table and proxy entries. */
    if (dataBlock->proxyTableSize)
    {
        /* Get pointers. */
        HlPACxV2ProxyEntryTable* proxyEntryTable =
            hlPACxV2DataGetProxyEntryTable(dataBlock);

        HlPACxV2ProxyEntry* proxyEntries = (HlPACxV2ProxyEntry*)
            hlOff32Get(&proxyEntryTable->proxyEntries);

        HlU32 i;

        /* Swap proxy entry table. */
        hlPACxV2ProxyEntryTableSwap(proxyEntryTable, HL_FALSE);

        /* Swap proxy entries. */
        for (i = 0; i < proxyEntryTable->proxyEntryCount; ++i)
        {
            hlPACxV2ProxyEntrySwap(&proxyEntries[i], HL_FALSE);
        }
    }
}

void hlPACxV2BlockHeaderFix(HlPACxV2BlockHeader* HL_RESTRICT blockHeader,
    HlU8 endianFlag, HlPACxV2Header* HL_RESTRICT header)
{
    switch (blockHeader->signature)
    {
    case HL_BINAV2_BLOCK_TYPE_DATA:
    {
        /* Swap block data header if necessary. */
        HlPACxV2BlockDataHeader* dataHeader = (HlPACxV2BlockDataHeader*)blockHeader;
        if (hlBINANeedsSwap(endianFlag))
        {
            hlPACxV2DataHeaderSwap(dataHeader);
        }

        /* Fix offsets. */
        {
            const void* offsets = hlPACxV2DataGetOffsetTable(dataHeader);
            hlBINAOffsetsFix32(offsets, endianFlag,
                dataHeader->offsetTableSize, header);
        }

        /* Swap data block if necessary. */
        if (hlBINANeedsSwap(endianFlag))
        {
            hlINPACxV2DataBlockSwapRecursive(dataHeader);
        }
        break;
    }

    default:
        HL_ASSERT(HL_FALSE);
        break;
    }
}

void hlPACxV2BlocksFix(HlPACxV2BlockHeader* HL_RESTRICT curBlock,
    HlU16 blockCount, HlU8 endianFlag, HlPACxV2Header* HL_RESTRICT header)
{
    HlU16 i;

    /* Return early if there are no blocks to fix. */
    if (!blockCount) return;

    /* Fix the first block's header. */
    hlPACxV2BlockHeaderFix(curBlock, endianFlag, header);

    /* Fix subsequent block headers, if any. */
    for (i = 1; i < blockCount; ++i)
    {
        /* Get next block. */
        curBlock = hlBINAV2BlockGetNext(curBlock);

        /* Fix this block's header. */
        hlPACxV2BlockHeaderFix(curBlock, endianFlag, header);
    }
}

void hlPACxV2Fix(HlBlob* blob)
{
    /* Swap header if necessary. */
    HlPACxV2Header* header = (HlPACxV2Header*)blob->data;
    if (hlBINANeedsSwap(header->endianFlag))
    {
        hlBINAV2HeaderSwap(header);
    }

    /* Fix blocks. */
    {
        HlPACxV2BlockHeader* blocks = (HlPACxV2BlockHeader*)(header + 1);
        hlPACxV2BlocksFix(blocks, header->blockCount,
            header->endianFlag, header);
    }
}

HlPACxV2NodeTree* hlPACxV2DataGetFileTree(
    HlPACxV2BlockDataHeader* HL_RESTRICT dataBlock,
    const char* HL_RESTRICT resType)
{
    HlPACxV2NodeTree* typeTree = hlPACxV2DataGetTypeTree(dataBlock);
    HlPACxV2Node* typeNodes = (HlPACxV2Node*)hlOff32Get(&typeTree->nodes);
    HlU32 i;

    for (i = 0; i < typeTree->nodeCount; ++i)
    {
        const char* typeStr = (const char*)hlOff32Get(&typeNodes[i].name);
        if (!strcmp(strchr(typeStr, ':') + 1, resType))
        {
            return (HlPACxV2NodeTree*)hlOff32Get(
                &typeNodes[i].data);
        }
    }

    return NULL;
}

typedef struct HlINPACxV2MergedStrEntry
{
    const char* srcStrPtr;
    HlU8* dstStrPtr;
}
HlINPACxV2MergedStrEntry;

static size_t hlINPACxV2FileTreeGetReqSize(
    const HlPACxV2NodeTree* HL_RESTRICT fileTree,
    HlBool skipProxies, const HlU32* HL_RESTRICT curOffset,
    const HlU8* HL_RESTRICT strings, const HlU8* HL_RESTRICT offsets,
    const HlU8* HL_RESTRICT eof, size_t* HL_RESTRICT entryCount)
{
    /* Get nodes pointer. */
    const HlPACxV2Node* nodes = (const HlPACxV2Node*)hlOff32Get(&fileTree->nodes);
    size_t reqBufSize = 0;
    HlU32 i;

    for (i = 0; i < fileTree->nodeCount; ++i)
    {
        /* Get pointers. */
        const HlPACxV2DataEntry* dataEntry = (const HlPACxV2DataEntry*)
            hlOff32Get(&nodes[i].data);

        const char* fileName;

        /* Skip proxies if requested. */
        if (skipProxies && (dataEntry->flags & HL_PACXV2_DATA_FLAGS_NOT_HERE))
            continue;

        /* Get file name pointer. */
        fileName = (const char*)hlOff32Get(&nodes[i].name);

        /* Increment entry count. */
        ++(*entryCount);

        /* Account for data. */
        if (!(dataEntry->flags & HL_PACXV2_DATA_FLAGS_NOT_HERE))
        {
            const HlU32* data = (const HlU32*)(dataEntry + 1);
            const HlU32* dataEnd = (const HlU32*)HL_ADD_OFFC(
                data, dataEntry->dataSize);

            HlBool isMergedBINA = HL_FALSE;

            /* Account for data. */
            reqBufSize += dataEntry->dataSize;

            /* Determine if this is a "merged" BINA file. */
            while (HL_TRUE)
            {
                if (curOffset >= data)
                {
                    if (curOffset < dataEnd)
                    {
                        /* This is a merged BINA file. */
                        isMergedBINA = HL_TRUE;
                    }

                    /* Now we know if this is a merged BINA file or not; we can stop checking. */
                    break;
                }

                /*
                    Get the next offset's address - return early
                    if we've reached the end of the offset table.
                */
                if (offsets >= eof || !hlBINAOffsetsNext(&offsets, &curOffset))
                    break;
            }

            /* Account for size required to "unmerge" this "merged" BINA file. */
            if (isMergedBINA)
            {
                HL_LIST(const char*) strPtrs;
                const HlU32* prevOffset = data;
                size_t strTableSize = 0, offTableSize = 0;

                /* Initialize string pointers list. */
                /* TODO: Use stack unless we run out of space, then switch to heap. */
                HL_LIST_INIT(strPtrs);

                /* Account for BINA header. */
                reqBufSize += sizeof(HlBINAV2Header);

                /* Account for BINA data block header and padding. */
                reqBufSize += (sizeof(HlBINAV2BlockDataHeader) * 2);

                while (HL_TRUE)
                {
                    /* Stop if this offset is not part of the current file. */
                    if (curOffset >= dataEnd) break;

                    /* Account for strings. */
                    {
                        /* Get the value the current offset points to. */
                        const HlU8* curOffsetVal = (const HlU8*)hlOff32Get(curOffset);

                        /* Account for strings. */
                        if (curOffsetVal >= strings)
                        {
                            /*
                                Ensure this string pointer is not already
                                in the string pointers list.
                            */
                            size_t i2;
                            HlBool skipString = HL_FALSE;

                            for (i2 = 0; i2 < strPtrs.count; ++i2)
                            {
                                if (strPtrs.data[i2] == (const char*)curOffsetVal)
                                {
                                    skipString = HL_TRUE;
                                    break;
                                }
                            }

                            if (!skipString)
                            {
                                /* Add string pointer to string pointers list. */
                                if (HL_FAILED(HL_LIST_PUSH(strPtrs,
                                    (const char*)curOffsetVal)))
                                {
                                    HL_ASSERT(HL_FALSE);
                                    return 0;
                                }

                                strTableSize += (strlen((const char*)curOffsetVal) + 1);
                            }
                        }
                    }

                    /* Account for offset table entry. */
                    {
                        const HlU32 offDiff = (HlU32)(curOffset - prevOffset);
                        if (offDiff <= 0x3FU)
                        {
                            /* Account for six-bit BINA offset table entry. */
                            ++offTableSize;
                        }
                        else if (offDiff <= 0x3FFFU)
                        {
                            /* Account for fourteen-bit BINA offset table entry. */
                            offTableSize += 2;
                        }
                        else
                        {
                            /*
                                Ensure offset difference is within 30 bits.
                                (This *ALWAYS* should be true, so if it's false,
                                something has seriously gone wrong.)
                            */
                            HL_ASSERT(offDiff <= 0x3FFFFFFFU);

                            /* Account for thirty-bit BINA offset table entry. */
                            offTableSize += 4;
                        }
                    }

                    /* Set previous offset pointer. */
                    prevOffset = curOffset;

                    /*
                        Get the next offset's address - return early
                        if we've reached the end of the offset table.
                    */
                    if (offsets >= eof || !hlBINAOffsetsNext(&offsets, &curOffset))
                        break;
                }

                /* Free string pointers list. */
                HL_LIST_FREE(strPtrs);

                /* Account for string table and padding. */
                reqBufSize += (strTableSize + ((((strTableSize + 3) &
                    ~((size_t)3))) - strTableSize));

                /* Account for offset table and padding. */
                reqBufSize += (offTableSize + ((((offTableSize + 3) &
                    ~((size_t)3))) - offTableSize));
            }
        }

        /* Account for name. */
#ifdef HL_IN_WIN32_UNICODE
        reqBufSize += (hlStrGetReqLenUTF8ToUTF16(fileName, 0) *
            sizeof(HlNChar));
#else
        reqBufSize += (strlen(fileName) + 1);
#endif
    }

    return reqBufSize;
}

static HlResult hlINPACxV2FileTreeSetupEntries(
    const HlPACxV2NodeTree* HL_RESTRICT fileTree, HlBool skipProxies,
    HlU8 endianFlag, const HlU32* HL_RESTRICT curOffset,
    const HlU8* HL_RESTRICT strings, const HlU8* HL_RESTRICT offsets,
    const HlU8* HL_RESTRICT eof, const char* HL_RESTRICT typeStr,
    size_t extLen, HlArchiveEntry** HL_RESTRICT curEntry,
    void** HL_RESTRICT curDataPtr)
{
    /* Get nodes pointer. */
    const HlPACxV2Node* nodes = (const HlPACxV2Node*)hlOff32Get(&fileTree->nodes);
    HlU32 i;

    for (i = 0; i < fileTree->nodeCount; ++i)
    {
        /* Get pointers. */
        const HlPACxV2DataEntry* dataEntry = (const HlPACxV2DataEntry*)
            hlOff32Get(&nodes[i].data);

        const char* fileName = (const char*)hlOff32Get(&nodes[i].name);

        /* Skip proxies if requested. */
        if (skipProxies && (dataEntry->flags & HL_PACXV2_DATA_FLAGS_NOT_HERE))
            continue;

        /* Set path and size within file entry. */
        (*curEntry)->path = (const HlNChar*)(*curDataPtr);
        (*curEntry)->size = (size_t)dataEntry->dataSize;

        /* Copy file name. */
        {
            HlNChar* curNamePtr = (HlNChar*)(*curDataPtr);
            size_t fileNameLen;

#ifdef HL_IN_WIN32_UNICODE
            /* Convert file name to UTF-16 and copy into buffer. */
            fileNameLen = hlStrConvUTF8ToUTF16NoAlloc(fileName,
                (HlChar16*)curNamePtr, 0, 0);

            if (!fileNameLen) return HL_ERROR_UNKNOWN;

            /* Convert extension to UTF-16 and copy into buffer. */
            curNamePtr[fileNameLen - 1] = HL_NTEXT('.');

            if (!hlStrConvUTF8ToUTF16NoAlloc(typeStr,
                &curNamePtr[fileNameLen], extLen, 0))
            {
                return HL_ERROR_UNKNOWN;
            }
#else
            /* Copy file name into buffer and get file name length. */
            fileNameLen = hlStrCopyAndLen(fileName, curNamePtr);

            /* Copy extension into buffer and increase fileNameLen. */
            curNamePtr[fileNameLen++] = HL_NTEXT('.');
            memcpy(&curNamePtr[fileNameLen], typeStr, extLen);
#endif

            /* Increase fileNameLen. */
            fileNameLen += extLen;

            /* Set null terminator and increase curDataPtr. */
            curNamePtr[fileNameLen++] = HL_NTEXT('\0');
            *curDataPtr = &curNamePtr[fileNameLen];
        }

        /* Set meta and data within file entry. */
        if (!(dataEntry->flags & HL_PACXV2_DATA_FLAGS_NOT_HERE))
        {
            /* Copy data and "unmerge" merged BINA data if necessary. */
            const HlU32* data = (const HlU32*)(dataEntry + 1);
            const HlU32* dataEnd = (const HlU32*)HL_ADD_OFFC(
                data, dataEntry->dataSize);

            void* dstData;
            HlBool isMergedBINA = HL_FALSE;

            /* Set meta and data within file entry. */
            (*curEntry)->meta = 0;
            (*curEntry)->data = (HlUMax)((HlUPtr)(*curDataPtr));

            /* Determine if this is a "merged" BINA file. */
            while (HL_TRUE)
            {
                if (curOffset >= data)
                {
                    if (curOffset < dataEnd)
                    {
                        /* This is a merged BINA file. */
                        isMergedBINA = HL_TRUE;

                        /* Set dstData and increase curDataPtr. */
                        dstData = HL_ADD_OFF(*curDataPtr, sizeof(HlBINAV2Header) +
                            (sizeof(HlBINAV2BlockDataHeader) * 2));

                        *curDataPtr = dstData;
                    }

                    /* Now we know if this is a merged BINA file or not; we can stop checking. */
                    break;
                }

                /*
                    Get the next offset's address - return early
                    if we've reached the end of the offset table.
                */
                if (offsets >= eof || !hlBINAOffsetsNext(&offsets, &curOffset))
                    break;
            }

            /* Copy data. */
            /* TODO: Fix crash here. */
            memcpy(*curDataPtr, data, dataEntry->dataSize);

            /* Increase curDataPtr. */
            *curDataPtr = HL_ADD_OFF(*curDataPtr, dataEntry->dataSize);

            /* "Unmerge" this "merged" BINA file. */
            if (isMergedBINA)
            {
                const HlU32* prevOffset = data;
                size_t strTableSize = 0, offTableSize = 0;

                /* Copy strings and fix string offsets. */
                {
                    HL_LIST(HlINPACxV2MergedStrEntry) strPtrs;
                    const HlU32* firstDataOffset = curOffset;
                    const HlU8* firstDataOffsetPos = offsets;

                    /* Initialize string pointers list. */
                    /* TODO: Use stack unless we run out of space, then switch to heap. */
                    HL_LIST_INIT(strPtrs);

                    while (HL_TRUE)
                    {
                        const HlU8* curOffsetVal;

                        /* Stop if this offset is not part of the current file. */
                        if (curOffset >= dataEnd) break;

                        /* Get the value the current offset points to. */
                        curOffsetVal = (const HlU8*)hlOff32Get(curOffset);

                        /* Copy strings. */
                        if (curOffsetVal >= strings)
                        {
                            /*
                                Ensure this string pointer is not already
                                in the string pointers list.
                            */
                            size_t i2;
                            HlBool skipString = HL_FALSE;

                            for (i2 = 0; i2 < strPtrs.count; ++i2)
                            {
                                if (strPtrs.data[i2].srcStrPtr == (const char*)curOffsetVal)
                                {
                                    /* Fix string offset. */
                                    const HlU32 newStrOffset = (HlU32)(
                                        strPtrs.data[i2].dstStrPtr - (HlU8*)dstData);

                                    ((HlU32*)dstData)[curOffset - data] =
                                        (hlBINANeedsSwap(endianFlag)) ?
                                        hlSwapU32(newStrOffset) : newStrOffset;

                                    /* Skip this string. */
                                    skipString = HL_TRUE;
                                    break;
                                }
                            }

                            if (!skipString)
                            {
                                {
                                    const HlINPACxV2MergedStrEntry strPtrEntry =
                                    {
                                        (const char*)curOffsetVal,
                                        (HlU8*)(*curDataPtr)
                                    };

                                    const HlU32 newStrOffset = (HlU32)(
                                        strPtrEntry.dstStrPtr - (HlU8*)dstData);

                                    /* Add string pointer to string pointers list. */
                                    if (HL_FAILED(HL_LIST_PUSH(strPtrs, strPtrEntry)))
                                    {
                                        HL_ASSERT(HL_FALSE);
                                        return 0;
                                    }

                                    /* Fix string offset. */
                                    ((HlU32*)dstData)[curOffset - data] =
                                        (hlBINANeedsSwap(endianFlag)) ?
                                        hlSwapU32(newStrOffset) : newStrOffset;
                                }

                                {
                                    /* Copy string and get length. */
                                    char* curStr = (char*)(*curDataPtr);
                                    size_t strSize = (hlStrCopyAndLen(
                                        (const char*)curOffsetVal,
                                        curStr + 1));

                                    /* Increase strTableSize and curDataPtr. */
                                    strTableSize += strSize;
                                    curStr += strSize;
                                    *curDataPtr = curStr;
                                }
                            }
                        }

                        /*
                            Get the next offset's address - return early
                            if we've reached the end of the offset table.
                        */
                        if (offsets >= eof || !hlBINAOffsetsNext(&offsets, &curOffset))
                            break;
                    }

                    /* Free string pointers list. */
                    HL_LIST_FREE(strPtrs);

                    /* Reset curOffset and offsets pointers. */
                    curOffset = firstDataOffset;
                    offsets = firstDataOffsetPos;
                }

                /* Pad string table. */
                {
                    /* Compute string table padding. */
                    const size_t strTablePadding = ((((strTableSize + 3) &
                        ~((size_t)3))) - strTableSize);

                    /* Pad string table. */
                    memset(*curDataPtr, 0, strTablePadding);
                    
                    /* Increase strTableSize and curDataPtr. */
                    strTableSize += strTablePadding;
                    *curDataPtr = HL_ADD_OFF(*curDataPtr, strTablePadding);
                }

                /* Setup offset table entries and fix non-string offsets. */
                while (HL_TRUE)
                {
                    const HlU8* curOffsetVal;

                    /* Stop if this offset is not part of the current file. */
                    if (curOffset >= dataEnd) break;

                    /* Get the value the current offset points to. */
                    curOffsetVal = (const HlU8*)hlOff32Get(curOffset);

                    /* Fix non-string offsets. */
                    if (curOffsetVal < strings)
                    {
                        const HlU32 newOffset = (HlU32)(
                            curOffsetVal - (const HlU8*)data);

                        ((HlU32*)dstData)[curOffset - data] =
                            (hlBINANeedsSwap(endianFlag)) ?
                            hlSwapU32(newOffset) : newOffset;
                    }

                    /* Setup offset table entry. */
                    {
                        HlU8* offEntry = (HlU8*)(*curDataPtr);
                        const HlU32 offDiff = (HlU32)(curOffset - prevOffset);

                        if (offDiff <= 0x3FU)
                        {
                            /* Setup six-bit BINA offset table entry. */
                            *(offEntry++) = (HL_BINA_OFF_SIZE_SIX_BIT | (HlU8)offDiff);

                            /* Increase offTableSize. */
                            ++offTableSize;
                        }
                        else if (offDiff <= 0x3FFFU)
                        {
                            /* Setup fourteen-bit BINA offset table entry. */
                            *(offEntry++) = (HL_BINA_OFF_SIZE_FOURTEEN_BIT |
                                (HlU8)(offDiff >> 8));

                            *(offEntry++) = (HlU8)(offDiff & 0xFF);

                            /* Increase offTableSize. */
                            offTableSize += 2;
                        }
                        else
                        {
                            /*
                                Ensure offset difference is within 30 bits.
                                (This *ALWAYS* should be true, so if it's false,
                                something has seriously gone wrong.)
                            */
                            HL_ASSERT(offDiff <= 0x3FFFFFFFU);

                            /* Setup thirty-bit BINA offset table entry. */
                            *(offEntry++) = (HL_BINA_OFF_SIZE_THIRTY_BIT |
                                (HlU8)(offDiff >> 24));

                            *(offEntry++) = (HlU8)((offDiff & 0xFF0000) >> 16);
                            *(offEntry++) = (HlU8)((offDiff & 0xFF00) >> 8);
                            *(offEntry++) = (HlU8)(offDiff & 0xFF);

                            /* Increase offTableSize. */
                            offTableSize += 4;
                        }

                        /* Increase curDataPtr. */
                        *curDataPtr = offEntry;
                    }

                    /* Set previous offset pointer. */
                    prevOffset = curOffset;

                    /*
                        Get the next offset's address - return early
                        if we've reached the end of the offset table.
                    */
                    if (offsets >= eof || !hlBINAOffsetsNext(&offsets, &curOffset))
                        break;
                }

                /* Pad offset table. */
                {
                    /* Compute string table padding. */
                    const size_t offTablePadding = ((((offTableSize + 3) &
                        ~((size_t)3))) - offTableSize);

                    /* Pad offset table. */
                    memset(*curDataPtr, 0, offTablePadding);

                    /* Increase offTableSize and curDataPtr. */
                    offTableSize += offTablePadding;
                    *curDataPtr = HL_ADD_OFF(*curDataPtr, offTablePadding);
                }

                /*
                   Increase current entry size to account for header,
                   data block, and string/offset tables.
                */
                (*curEntry)->size += sizeof(HlBINAV2Header) +
                    (sizeof(HlBINAV2BlockDataHeader) * 2);

                (*curEntry)->size += (strTableSize + offTableSize);

                /* Setup BINA header and data block header. */
                {
                    HlBINAV2Header* dstHeader = (HlBINAV2Header*)(
                        (HlUPtr)((*curEntry)->data));

                    HlBINAV2BlockDataHeader* dstDataBlock =
                        (HlBINAV2BlockDataHeader*)(dstHeader + 1);

                    /* Setup BINA header. */
                    dstHeader->signature = HL_BINA_SIG;
                    dstHeader->version[0] = '2';
                    dstHeader->version[1] = '0';
                    dstHeader->version[2] = '0';
                    dstHeader->endianFlag = endianFlag;
                    dstHeader->fileSize = (HlU32)((*curEntry)->size);
                    dstHeader->blockCount = 1;
                    dstHeader->padding = 0;

                    /* Setup BINA data block header. */
                    dstDataBlock->signature = HL_BINAV2_BLOCK_TYPE_DATA;
                    dstDataBlock->size = (dstHeader->fileSize - sizeof(HlBINAV2Header));
                    dstDataBlock->stringTableOffset = dataEntry->dataSize;
                    dstDataBlock->stringTableSize = (HlU32)strTableSize;
                    dstDataBlock->offsetTableSize = (HlU32)offTableSize;
                    dstDataBlock->relativeDataOffset = sizeof(HlBINAV2BlockDataHeader);
                    dstDataBlock->padding = 0;

                    /* Set padding. */
                    memset(dstDataBlock + 1, 0, sizeof(HlBINAV2BlockDataHeader));

                    /* Endian swap header and data block header if necessary. */
                    if (hlBINANeedsSwap(endianFlag))
                    {
                        hlBINAV2HeaderSwap(dstHeader);
                        hlBINAV2DataHeaderSwap(dstDataBlock, HL_TRUE);
                    }
                }
            }
        }
        else
        {
            /* Set meta and data within file entry. */
            (*curEntry)->meta = HL_ARC_ENTRY_STREAMING_FLAG;
            (*curEntry)->data = 0;
        }

        /* Increase current entry pointer. */
        ++(*curEntry);
    }

    return HL_RESULT_SUCCESS;
}

HlResult hlPACxV2Read(const HlBlob** HL_RESTRICT pacs,
    size_t pacCount, HlArchive** HL_RESTRICT archive)
{
    void* hlArcBuf;
    HlArchiveEntry* curEntry;
    size_t entryCount = 0;
    const HlBool skipProxies = (pacCount > 1);

    /* Allocate HlArchive buffer. */
    {
        /* Compute required size for HlArchive buffer. */
        size_t i, reqBufSize = sizeof(HlArchive);
        for (i = 0; i < pacCount; ++i)
        {
            /* Get data block pointer. */
            const HlPACxV2BlockDataHeader* dataBlock = (const
                HlPACxV2BlockDataHeader*)hlBINAV2GetDataBlock(pacs[i]);

            const HlU32* firstDataOffset;
            const HlU8 *strings, *offsets, *eof;

            if (!dataBlock) return HL_ERROR_INVALID_DATA;

            /* Get current offset pointer. */
            firstDataOffset = (const HlU32*)pacs[i]->data;

            /* Get strings, offsets, and eof pointers. */
            strings = (const HlU8*)hlPACxV2DataGetStringTable(dataBlock);
            offsets = (const HlU8*)hlPACxV2DataGetOffsetTable(dataBlock);
            eof = (offsets + dataBlock->offsetTableSize);

            /* OPTIMIZATION: Skip through all offsets that aren't part of the data. */
            {
                const HlU32* dataEntries = (const HlU32*)
                    hlPACxV2DataGetDataEntries(dataBlock);

                while (offsets < eof)
                {
                    /* Break if we've reached an offset within the data entries. */
                    if (firstDataOffset >= dataEntries) break;

                    /*
                        Get the next offset's address - break if
                        we've reached the end of the offset table.
                    */
                    if (!hlBINAOffsetsNext(&offsets, &firstDataOffset))
                        break;
                }
            }

            /* Account for file trees. */
            {
                /* Get pointers. */
                const HlPACxV2NodeTree* typeTree = (const HlPACxV2NodeTree*)
                    (dataBlock + 1);

                const HlPACxV2Node* typeNodes = (const HlPACxV2Node*)
                    hlOff32Get(&typeTree->nodes);

                HlU32 i2;

                /* Account for file trees. */
                for (i2 = 0; i2 < typeTree->nodeCount; ++i2)
                {
                    const HlPACxV2NodeTree* fileTree;
                    const char* typeStr = (const char*)hlOff32Get(&typeNodes[i2].name);
                    const char* colonPtr = strchr(typeStr, ':');
                    size_t extSize;

                    /* Skip ResPacDepend file trees. */
                    if (!strcmp(colonPtr + 1, HlINPACxV2SplitType)) continue;

                    /* Get fileTree pointer. */
                    fileTree = (const HlPACxV2NodeTree*)hlOff32Get(&typeNodes[i2].data);

                    /* Compute extension size. */
                    extSize = (size_t)(colonPtr - typeStr);

#ifdef HL_IN_WIN32_UNICODE
                    /* Account for UTF-8 -> UTF-16 conversion and dot. */
                    extSize = ((hlStrGetReqLenUTF8ToUTF16(typeStr, extSize) +
                        1) * sizeof(HlNChar));
#else
                    /* Account for dot. */
                    ++extSize;
#endif

                    /* Account for file trees. */
                    reqBufSize += hlINPACxV2FileTreeGetReqSize(
                        fileTree, skipProxies, firstDataOffset, strings,
                        offsets, eof, &entryCount);

                    /* Account for extension size. */
                    reqBufSize += (extSize * fileTree->nodeCount);
                }
            }
        }

        /* Account for archive entries. */
        reqBufSize += (sizeof(HlArchiveEntry) * entryCount);

        /* Allocate archive buffer. */
        hlArcBuf = hlAlloc(reqBufSize);
        if (!hlArcBuf) return HL_ERROR_OUT_OF_MEMORY;
    }

    /* Setup HlArchive. */
    {
        /* Get pointers. */
        HlArchive* arc = (HlArchive*)hlArcBuf;
        curEntry = (HlArchiveEntry*)(arc + 1);

        /* Setup HlArchive. */
        arc->entries = curEntry;
        arc->entryCount = entryCount;
    }

    /* Setup archive entries. */
    {
        void* curDataPtr = &curEntry[entryCount];
        size_t i;
        HlResult result;

        for (i = 0; i < pacCount; ++i)
        {
            /* Get data block pointer. */
            const HlPACxV2BlockDataHeader* dataBlock = (const
                HlPACxV2BlockDataHeader*)hlBINAV2GetDataBlock(pacs[i]);

            const HlU32* firstDataOffset;
            const HlU8 *strings, *offsets, *eof;

            if (!dataBlock) return HL_ERROR_INVALID_DATA;

            /* Get current offset pointer. */
            firstDataOffset = (const HlU32*)pacs[i]->data;

            /* Get strings, offsets, and eof pointers. */
            strings = (const HlU8*)hlPACxV2DataGetStringTable(dataBlock);
            offsets = (const HlU8*)hlPACxV2DataGetOffsetTable(dataBlock);
            eof = (offsets + dataBlock->offsetTableSize);

            /* OPTIMIZATION: Skip through all offsets that aren't part of the data. */
            {
                const HlU32* dataEntries = (const HlU32*)
                    hlPACxV2DataGetDataEntries(dataBlock);

                while (offsets < eof)
                {
                    /* Break if we've reached an offset within the data entries. */
                    if (firstDataOffset >= dataEntries) break;

                    /*
                        Get the next offset's address - break if
                        we've reached the end of the offset table.
                    */
                    if (!hlBINAOffsetsNext(&offsets, &firstDataOffset))
                        break;
                }
            }

            /* Setup file entries in this pac. */
            {
                /* Get pointers. */
                const HlPACxV2NodeTree* typeTree = (const HlPACxV2NodeTree*)
                    (dataBlock + 1);

                const HlPACxV2Node* typeNodes = (const HlPACxV2Node*)
                    hlOff32Get(&typeTree->nodes);

                HlU32 i2;

                /* Setup file entries in this pac. */
                for (i2 = 0; i2 < typeTree->nodeCount; ++i2)
                {
                    const HlPACxV2NodeTree* fileTree;
                    const char* typeStr = (const char*)hlOff32Get(&typeNodes[i2].name);
                    const char* colonPtr = strchr(typeStr, ':');
                    size_t extLen;

                    /* Skip ResPacDepend file trees. */
                    if (!strcmp(colonPtr + 1, HlINPACxV2SplitType)) continue;

                    /* Get fileTree pointer. */
                    fileTree = (const HlPACxV2NodeTree*)hlOff32Get(&typeNodes[i2].data);

                    /* Compute extension length. */
                    extLen = (size_t)(colonPtr - typeStr);

#ifdef HL_IN_WIN32_UNICODE
                    /* Account for UTF-8 -> UTF-16 conversion. */
                    extLen = hlStrGetReqLenUTF8ToUTF16(typeStr, extLen);
#endif

                    /* Setup file entries. */
                    result = hlINPACxV2FileTreeSetupEntries(fileTree,
                        skipProxies, ((const HlPACxV2Header*)pacs[i]->data)->endianFlag,
                        firstDataOffset, strings, offsets, eof, typeStr, extLen,
                        &curEntry, &curDataPtr);

                    if (HL_FAILED(result))
                    {
                        hlFree(hlArcBuf);
                        return result;
                    }
                }
            }
        }
    }

    /* Set archive pointer and return success. */
    *archive = (HlArchive*)hlArcBuf;
    return HL_RESULT_SUCCESS;
}

static HlResult hlINPACxV2LoadSingle(const HlNChar* HL_RESTRICT filePath,
    HlArchive** HL_RESTRICT archive)
{
    HlBlob* blob;
    HlResult result;

    /* Load archive. */
    result = hlBlobLoad(filePath, &blob);
    if (HL_FAILED(result)) return result;

    /* Fix PACxV2 data. */
    hlPACxV2Fix(blob);

    /* Parse blob into HlArchive, free blob, and return. */
    result = hlPACxV2Read(&blob, 1, archive);
    hlFree(blob);
    return result;
}

static HlResult hlINPACxV2LoadSplits(const HlNChar* HL_RESTRICT filePath,
    HlArchive** HL_RESTRICT archive)
{
    HlBlob** pacs = NULL;
    HlBlob* rootPac = NULL;
    HlNChar pathBuf[255];
    HlNChar* pathBufPtr = pathBuf;
    size_t dirLen, pacCount = 0, pathBufCapacity = 255;
    HlResult result = HL_RESULT_SUCCESS;

    /* Allocate pac path buffer. */
    {
        size_t filePathLen = hlNStrLen(filePath);
        const HlNChar* ext = hlPathGetExt(filePath, filePathLen);
        HlBool needsSep1;
        
        /* Get directory length. */
        dirLen = (hlPathGetName(filePath) - filePath);
        
        /* Get whether the first path needs a separator appended to it. */
        needsSep1 = hlINPathCombineNeedsSep1(filePath, dirLen);
        if (needsSep1) ++filePathLen;

        /* Determine whether this pac is a split. */
        if (ext[0] == HL_NTEXT('.') && HL_IS_DIGIT(ext[1]) &&
            HL_IS_DIGIT(ext[2]) && ext[3] == HL_NTEXT('\0'))
        {
            /* Create a copy of filePath big enough to hold just the root's name. */
            if ((filePathLen - 2) > pathBufCapacity)
            {
                pathBufPtr = HL_ALLOC_ARR(HlNChar, filePathLen - 2);
                if (!pathBufPtr) return HL_ERROR_OUT_OF_MEMORY;
            }

            filePathLen -= 3;
            memcpy(pathBufPtr, filePath, filePathLen * sizeof(HlNChar));
            
            /* Set separator if necessary. */
            if (needsSep1) pathBufPtr[filePathLen - 1] = HL_PATH_SEP;

            /* Set null terminator. */
            pathBufPtr[filePathLen] = HL_NTEXT('\0');
        }

        /* Otherwise, just assume it's a root pac. */
        else
        {
            /* Create a copy of filePath big enough to hold just the root's name. */
            if ((filePathLen + 1) > pathBufCapacity)
            {
                pathBufPtr = HL_ALLOC_ARR(HlNChar, filePathLen + 1);
                if (!pathBufPtr) return HL_ERROR_OUT_OF_MEMORY;
            }

            memcpy(pathBufPtr, filePath, filePathLen * sizeof(HlNChar));

            /* Set separator if necessary. */
            if (needsSep1) pathBufPtr[filePathLen - 1] = HL_PATH_SEP;

            /* Set null terminator. */
            pathBufPtr[filePathLen] = HL_NTEXT('\0');
        }
    }

    /* Load the root pac. */
    result = hlBlobLoad(pathBufPtr, &rootPac);
    if (HL_FAILED(result)) goto end;

    /* Fix root pac. */
    hlPACxV2Fix(rootPac);

    /* Load splits. */
    {
        /* Get data block. */
        HlPACxV2BlockDataHeader* dataBlock = (HlPACxV2BlockDataHeader*)
            hlBINAV2GetDataBlock(rootPac);

        /* TODO: Ensure data block pointer is not null. */

        /* Get split table file tree. */
        HlPACxV2NodeTree* splitFileTree = hlPACxV2DataGetFileTree(
            dataBlock, HlINPACxV2SplitType);

        HlPACxV2Node* splitFileNodes = (HlPACxV2Node*)hlOff32Get(
            &splitFileTree->nodes);

        HlU32 i;

        /* Get splits count. */
        for (i = 0; i < splitFileTree->nodeCount; ++i)
        {
            HlPACxV2SplitTable* splitTable = (HlPACxV2SplitTable*)(
                ((HlPACxV2DataEntry*)hlOff32Get(&splitFileNodes[i].data)) + 1);

            pacCount += (size_t)splitTable->splitCount;
        }

        /* Allocate splits pointer array. */
        pacs = HL_ALLOC_ARR(HlBlob*, pacCount + 1);
        if (!pacs)
        {
            result = HL_ERROR_OUT_OF_MEMORY;
            goto end;
        }

        /* Set root pointer. */
        pacs[0] = rootPac;

        /*
           Reset pac count. We'll increment it again in the next loop.
           This is so freeing can always work correctly.
        */
        pacCount = 1;

        /* Load splits. */
        for (i = 0; i < splitFileTree->nodeCount; ++i)
        {
            HlPACxV2SplitTable* splitTable = (HlPACxV2SplitTable*)(
                ((HlPACxV2DataEntry*)hlOff32Get(&splitFileNodes[i].data)) + 1);

            HL_OFF32_STR* splitNames = (HL_OFF32_STR*)hlOff32Get(
                &splitTable->splitNames);

            HlU32 i2;

            for (i2 = 0; i2 < splitTable->splitCount; ++i2)
            {
                const char* splitName = (const char*)hlOff32Get(&splitNames[i2]);
                size_t splitNameLen = (strlen(splitName) + 1), pathBufLen;

#ifdef HL_IN_WIN32_UNICODE
                splitNameLen = hlStrGetReqLenUTF8ToUTF16(splitName, splitNameLen);
#endif

                /* Account for directory. */
                pathBufLen = (splitNameLen + dirLen);

                /* Resize buffer if necessary. */
                if (pathBufLen > pathBufCapacity)
                {
                    if (pathBufPtr == pathBuf)
                    {
                        /* Switch from stack buffer to heap buffer. */
                        pathBufPtr = HL_ALLOC_ARR(HlNChar, pathBufLen);
                    }
                    else
                    {
                        /* Resize heap buffer. */
                        pathBufPtr = HL_RESIZE_ARR(HlNChar, pathBufLen, pathBufPtr);
                    }

                    if (!pathBufPtr)
                    {
                        result = HL_ERROR_OUT_OF_MEMORY;
                        goto end;
                    }
                }

                /* Copy split name into buffer. */
#ifdef HL_IN_WIN32_UNICODE
                if (!hlStrConvUTF8ToUTF16NoAlloc(splitName, 
                    (HlChar16*)&pathBufPtr[dirLen], 0, pathBufCapacity))
                {
                    result = HL_ERROR_UNKNOWN;
                    goto end;
                }
#else
                memcpy(&pathBufPtr[dirLen], splitName, splitNameLen * sizeof(HlNChar));
#endif

                /* Load split. */
                result = hlBlobLoad(pathBufPtr, &pacs[pacCount]);
                if (HL_FAILED(result)) goto end;

                /* Fix split. */
                hlPACxV2Fix(pacs[pacCount++]);
            }
        }
    }

    /* Parse into HlArchive. */
    result = hlPACxV2Read(pacs, pacCount, archive);

end:
    /* Free splits. */
    {
        size_t i;
        for (i = 1; i < pacCount; ++i)
        {
            hlFree(pacs[i]);
        }

        hlFree(pacs);
    }

    /* Free root pac. */
    hlFree(rootPac);

    /* Free path buffer if necessary and return result. */
    if (pathBufPtr != pathBuf) hlFree(pathBufPtr);
    return result;
}

HlResult hlPACxV2Load(const HlNChar* HL_RESTRICT filePath,
    HlBool loadSplits, HlArchive** HL_RESTRICT archive)
{
    return (loadSplits) ?
        hlINPACxV2LoadSplits(filePath, archive) :
        hlINPACxV2LoadSingle(filePath, archive);
}

void hlPACxV3Fix(HlBlob* blob)
{
    HlPACxV3Header* header = (HlPACxV3Header*)blob->data;
    if (hlBINANeedsSwap(header->endianFlag))
    {
        /* TODO: Swap header. */
        HL_ASSERT(HL_FALSE);
    }

    /* Fix offsets. */
    {
        const void* offsets = hlPACxV3GetOffsetTable(header);
        hlBINAOffsetsFix64(offsets, header->endianFlag,
            header->offsetTableSize, header);
    }

    if (hlBINANeedsSwap(header->endianFlag))
    {
        HlPACxV3NodeTree* typeTree = hlPACxV3GetTypeTree(header);
        /* TODO: Endian swap EVERYTHING if necessary. */
        /* TODO: Support endian swapping "v4" stuff as well. */
    }
}

#ifdef HL_IN_WIN32_UNICODE
static size_t hlINPACxV3FileNodesGetReqSize(const HlPACxV3Node* HL_RESTRICT fileNodes,
    const HlPACxV3Node* HL_RESTRICT fileNode, HlBool skipProxies,
    char* HL_RESTRICT pathBuf, size_t* HL_RESTRICT entryCount)
{
    const HlS32* childIndices = (const HlS32*)hlOff64Get(&fileNode->childIndices);
    size_t reqBufSize = 0;
    HlU16 i;

    if (fileNode->hasData)
    {
        const HlPACxV3DataEntry* dataEntry = (const HlPACxV3DataEntry*)
            hlOff64Get(&fileNode->data);

        /* Skip proxies if requested. */
        if (!skipProxies || dataEntry->dataType != HL_PACXV3_DATA_TYPE_NOT_HERE)
        {
            /* Account for name without null terminator. */
            reqBufSize += (hlStrGetReqLenUTF8ToUTF16(pathBuf,
                (size_t)fileNode->bufStartIndex) * sizeof(HlNChar));

            /* Account for extension and null terminator (add 1 for dot). */
            reqBufSize += ((hlStrGetReqLenUTF8ToUTF16((const char*)
                hlOff64Get(&dataEntry->extension), 0) + 1) * sizeof(HlNChar));

            /* Account for data. */
            if (dataEntry->dataType != HL_PACXV3_DATA_TYPE_NOT_HERE)
                reqBufSize += (size_t)dataEntry->dataSize;

            /* Increment entry count. */
            ++(*entryCount);
        }
    }
    else if (fileNode->name)
    {
        /* Copy name into path buffer. */
        strcpy(&pathBuf[fileNode->bufStartIndex],
            (const char*)hlOff64Get(&fileNode->name));
    }

    /* Recurse through children. */
    for (i = 0; i < fileNode->childCount; ++i)
    {
        reqBufSize += hlINPACxV3FileNodesGetReqSize(fileNodes,
            &fileNodes[childIndices[i]], skipProxies, pathBuf,
            entryCount);
    }

    return reqBufSize;
}
#endif

static size_t hlINPACxV3FileTreeGetReqSize(
    const HlPACxV3NodeTree* HL_RESTRICT fileTree,
    HlBool skipProxies, size_t* HL_RESTRICT entryCount)
{
    const HlPACxV3Node* fileNodes = (const HlPACxV3Node*)hlOff64Get(&fileTree->nodes);

#ifdef HL_IN_WIN32_UNICODE
    char pathBuf[256]; /* PACxV3 names are hard-limited to 255, not including null terminator. */

    /* Set first character in path buffer to null terminator, just in-case(TM). */
    pathBuf[0] = '\0';

    /* Recursively get required size. */
    return hlINPACxV3FileNodesGetReqSize(fileNodes,
        fileNodes, skipProxies, pathBuf, entryCount);
#else
    const HlS32* fileDataIndices = (const HlS32*)hlOff64Get(&fileTree->dataNodeIndices);
    size_t reqBufSize = 0;
    HlU64 i;

    for (i = 0; i < fileTree->dataNodeCount; ++i)
    {
        const HlPACxV3Node* fileNode = &fileNodes[fileDataIndices[i]];
        const HlPACxV3DataEntry* dataEntry = (const HlPACxV3DataEntry*)
            hlOff64Get(&fileNode->data);

        /* Skip proxies if requested. */
        if (skipProxies && dataEntry->dataType == HL_PACXV3_DATA_TYPE_NOT_HERE)
            continue;

        /* Account for name without null terminator. */
        reqBufSize += (size_t)fileNode->bufStartIndex;

        /* Account for extension and null terminator (add 2 for dot and null terminator). */
        reqBufSize += (strlen((const char*)hlOff64Get(&dataEntry->extension)) + 2);

        /* Account for data. */
        if (dataEntry->dataType != HL_PACXV3_DATA_TYPE_NOT_HERE)
            reqBufSize += (size_t)dataEntry->dataSize;

        /* Increment entry count. */
        ++(*entryCount);
    }

    return reqBufSize;
#endif
}

static HlResult hlINPACxV3FileNodesSetupEntries(const HlPACxV3Node* HL_RESTRICT fileNodes,
    const HlPACxV3Node* HL_RESTRICT fileNode, HlBool skipProxies,
    char* HL_RESTRICT pathBuf, HlArchiveEntry** HL_RESTRICT curEntry,
    void** HL_RESTRICT curDataPtr)
{
    const HlS32* childIndices = (const HlS32*)hlOff64Get(&fileNode->childIndices);
    HlU16 i;

    if (fileNode->hasData)
    {
        const HlPACxV3DataEntry* dataEntry = (const HlPACxV3DataEntry*)
            hlOff64Get(&fileNode->data);

        /* Skip proxies if requested. */
        if (!skipProxies || dataEntry->dataType != HL_PACXV3_DATA_TYPE_NOT_HERE)
        {
            HlNChar* curStrPtr = (HlNChar*)(*curDataPtr);

            /* Set path and size within archive entry. */
            (*curEntry)->path = curStrPtr;
            (*curEntry)->size = (size_t)dataEntry->dataSize;

#ifdef HL_IN_WIN32_UNICODE
            /* Convert to UTF-16, copy into HlArchive buffer, and increment curStrPtr. */
            curStrPtr += hlStrConvUTF8ToUTF16NoAlloc(pathBuf, curStrPtr,
                fileNode->bufStartIndex, 0);
#else
            /* Copy name into HlArchive buffer. */
            strcpy(curStrPtr, pathBuf);

            /* Increment curStrPtr. */
            curStrPtr += fileNode->bufStartIndex;
#endif

            /* Put dot for extension in HlArchive buffer. */
            *curStrPtr++ = HL_NTEXT('.');

            /* Copy extension into HlArchive buffer and increase curStrPtr. */
#ifdef HL_IN_WIN32_UNICODE
            curStrPtr += hlStrConvUTF8ToUTF16NoAlloc((const char*)hlOff64Get(
                &dataEntry->extension), curStrPtr, 0, 0);
#else
            curStrPtr += (hlStrCopyAndLen((const char*)hlOff64Get(
                &dataEntry->extension), curStrPtr) + 1);
#endif

            /* Increase curDataPtr. */
            *curDataPtr = curStrPtr;

            /* Set meta and data within archive entry and copy data if necessary. */
            if (dataEntry->dataType != HL_PACXV3_DATA_TYPE_NOT_HERE)
            {
                /* Set meta and data within archive entry. */
                (*curEntry)->meta = 0;
                (*curEntry)->data = (HlUMax)((HlUPtr)(*curDataPtr));

                /* Copy data. */
                memcpy(*curDataPtr, hlOff64Get(&dataEntry->data),
                    (size_t)dataEntry->dataSize);
                
                /* Increase curDataPtr. */
                *curDataPtr = HL_ADD_OFF(*curDataPtr, dataEntry->dataSize);
            }
            else
            {
                /* Set meta and data within archive entry. */
                (*curEntry)->meta = HL_ARC_ENTRY_STREAMING_FLAG;
                (*curEntry)->data = 0; /* TODO: Set this to something else? */
            }

            /* Increment current entry pointer. */
            ++(*curEntry);
        }
    }
    else if (fileNode->name)
    {
        /* Copy name into path buffer. */
        strcpy(&pathBuf[fileNode->bufStartIndex],
            (const char*)hlOff64Get(&fileNode->name));
    }

    /* Recurse through children. */
    for (i = 0; i < fileNode->childCount; ++i)
    {
        HlResult result = hlINPACxV3FileNodesSetupEntries(fileNodes,
            &fileNodes[childIndices[i]], skipProxies, pathBuf,
            curEntry, curDataPtr);

        if (HL_FAILED(result)) return result;
    }

    return HL_RESULT_SUCCESS;
}

HlResult hlPACxV3Read(const HlBlob** HL_RESTRICT pacs,
    size_t pacCount, HlArchive** HL_RESTRICT archive)
{
    void* hlArcBuf;
    HlArchiveEntry* curEntry;
    size_t entryCount = 0;
    const HlBool skipProxies = (pacCount > 1);

    /* Allocate HlArchive buffer. */
    {
        /* Get required buffer size. */
        size_t i, reqBufSize = sizeof(HlArchive);
        for (i = 0; i < pacCount; ++i)
        {
            const HlPACxV3Header* header = (const HlPACxV3Header*)pacs[i]->data;
            const HlPACxV3NodeTree* typeTree = hlPACxV3GetTypeTree(header);
            const HlPACxV3Node* typeNodes = (const HlPACxV3Node*)hlOff64Get(&typeTree->nodes);
            const HlS32* typeDataIndices = (const HlS32*)hlOff64Get(&typeTree->dataNodeIndices);
            HlU64 i2;

            for (i2 = 0; i2 < typeTree->dataNodeCount; ++i2)
            {
                /* Account for file nodes. */
                const HlPACxV3NodeTree* fileTree = (const HlPACxV3NodeTree*)
                    hlOff64Get(&typeNodes[typeDataIndices[i2]].data);

                reqBufSize += hlINPACxV3FileTreeGetReqSize(fileTree,
                    skipProxies, &entryCount);
            }
        }

        /* Account for archive entries. */
        reqBufSize += (sizeof(HlArchiveEntry) * entryCount);

        /* Allocate HlArchive buffer. */
        hlArcBuf = hlAlloc(reqBufSize);
        if (!hlArcBuf) return HL_ERROR_OUT_OF_MEMORY;
    }

    /* Setup HlArchive. */
    {
        /* Get pointers. */
        HlArchive* arc = (HlArchive*)hlArcBuf;
        curEntry = (HlArchiveEntry*)(arc + 1);

        /* Setup HlArchive. */
        arc->entries = curEntry;
        arc->entryCount = entryCount;
    }

    /* Setup archive entries. */
    {
        void* curDataPtr = &curEntry[entryCount];
        char pathBuf[256]; /* PACxV3 names are hard-limited to 255, not including null terminator. */
        size_t i;
        HlResult result;

        for (i = 0; i < pacCount; ++i)
        {
            const HlPACxV3Header* header = (const HlPACxV3Header*)pacs[i]->data;
            const HlPACxV3NodeTree* typeTree = hlPACxV3GetTypeTree(header);
            const HlPACxV3Node* typeNodes = (const HlPACxV3Node*)hlOff64Get(&typeTree->nodes);
            const HlS32* typeDataIndices = (const HlS32*)hlOff64Get(&typeTree->dataNodeIndices);
            HlU64 i2;

            for (i2 = 0; i2 < typeTree->dataNodeCount; ++i2)
            {
                /* Setup archive entries. */
                const HlPACxV3NodeTree* fileTree = (const HlPACxV3NodeTree*)
                    hlOff64Get(&typeNodes[typeDataIndices[i2]].data);

                const HlPACxV3Node* fileNodes = (const HlPACxV3Node*)
                    hlOff64Get(&fileTree->nodes);

                result = hlINPACxV3FileNodesSetupEntries(fileNodes, fileNodes,
                    skipProxies, pathBuf, &curEntry, &curDataPtr);
            }
        }
    }

    /* Set archive pointer and return success. */
    *archive = (HlArchive*)hlArcBuf;
    return HL_RESULT_SUCCESS;
}

HlResult hlPACxV3Load(const HlNChar* HL_RESTRICT filePath,
    HlBool loadSplits, HlArchive** HL_RESTRICT archive)
{
    /* TODO */
    return HL_ERROR_UNKNOWN;
}

void hlPACxV4Fix(HlBlob* blob)
{
    /* Swap header and root chunks if necessary. */
    HlPACxV4Header* header = (HlPACxV4Header*)blob->data;
    if (hlBINANeedsSwap(header->endianFlag))
    {
        /* TODO: Swap header. */
        /* TODO: Swap root chunks. */
        HL_ASSERT(HL_FALSE);
    }

    /* Fix offsets. */
    hlOff32Fix(&header->rootOffset, header);
}

HlResult hlPACxV4DecompressNoAlloc(const void* HL_RESTRICT compressedData,
    const HlPACxV4Chunk* HL_RESTRICT chunks, HlU32 chunkCount,
    HlU32 uncompressedSize, void* HL_RESTRICT uncompressedData)
{
    const char* compressedPtr = (const char*)compressedData;
    char* uncompressedPtr = (char*)uncompressedData;
    HlU32 i;

    for (i = 0; i < chunkCount; ++i)
    {
        /* Decompress the current chunk. */
        int r = LZ4_decompress_safe(compressedPtr,
            uncompressedPtr, chunks[i].compressedSize,
            uncompressedSize);

        /* Return HL_ERROR_UNKNOWN if decompressing failed. */
        if (r < 0 || (HlU32)r < chunks[i].uncompressedSize)
            return HL_ERROR_UNKNOWN;

        /* Substract from uncompressedSize. */
        uncompressedSize -= chunks[i].uncompressedSize;

        /* Increment pointers. */
        compressedPtr += chunks[i].compressedSize;
        uncompressedPtr += chunks[i].uncompressedSize;
    }

    return HL_RESULT_SUCCESS;
}

HlResult hlPACxV4Decompress(const void* HL_RESTRICT compressedData,
    const HlPACxV4Chunk* HL_RESTRICT chunks, HlU32 chunkCount,
    HlU32 uncompressedSize, void** HL_RESTRICT uncompressedData)
{
    void* uncompressedDataBuf;
    HlResult result;

    /* Allocate a buffer to hold the uncompressed data. */
    uncompressedDataBuf = hlAlloc(uncompressedSize);
    if (!uncompressedDataBuf) return HL_ERROR_OUT_OF_MEMORY;

    /* Decompress the data. */
    result = hlPACxV4DecompressNoAlloc(compressedData, chunks,
        chunkCount, uncompressedSize, uncompressedDataBuf);

    if (HL_FAILED(result)) return result;

    /* Set uncompressedData pointer and return success. */
    *uncompressedData = uncompressedDataBuf;
    return HL_RESULT_SUCCESS;
}

static HlResult hlINPACxV4DecompressAndFix(void* HL_RESTRICT data,
    const HlPACxV4Chunk* HL_RESTRICT chunks, HlU32 chunkCount,
    HlU32 compressedSize, HlU32 uncompressedSize,
    HlBlob* HL_RESTRICT blob)
{
    /* Decompress PAC first if necessary. */
    if (compressedSize != uncompressedSize)
    {
        void* uncompressedDataBuf;
        HlResult result;

        /* Decompress PAC. */
        result = hlPACxV4Decompress(data, chunks,
            chunkCount, uncompressedSize,
            &uncompressedDataBuf);

        if (HL_FAILED(result)) return result;

        /* Setup blob. */
        blob->data = uncompressedDataBuf;
        blob->size = (size_t)uncompressedSize;
    }
    else
    {
        /* Setup PAC entry. */
        blob->data = data;

        /*
           HACK: Set size to 0 to indicate uncompressed PAC since
           hlPACxV3Read doesn't utilize size anyway.
        */
        blob->size = 0;
    }

    /* Fix root PAC. */
    hlPACxV3Fix(blob);
    return HL_RESULT_SUCCESS;
}

HlResult hlPACxV4Read(HlBlob* HL_RESTRICT pac,
    HlBool loadSplits, HlArchive** HL_RESTRICT archive)
{
    const HlPACxV4Header* header = (const HlPACxV4Header*)pac->data;
    HlBlob** pacs = NULL;
    HlBlob root;
    size_t pacCount;
    HlResult result = HL_RESULT_SUCCESS;

    /* Setup root PAC entry, decompressing if necessary. */
    result = hlINPACxV4DecompressAndFix(hlOff32Get(&header->rootOffset),
        hlPACxV4GetRootChunks(header), header->chunkCount,
        header->rootCompressedSize, header->rootUncompressedSize,
        &root);

    if (HL_FAILED(result)) return result;

    /* Generate HlArchive and return. */
    if (loadSplits && ((HlPACxV3Header*)root.data)->splitCount)
    {
        HlPACxV3SplitTable* splitTable = hlPACxV3GetSplitTable(
            (HlPACxV3Header*)root.data);

        HlPACxV4SplitEntry* splitEntries = (HlPACxV4SplitEntry*)
            hlOff64Get(&splitTable->splitEntries);

        HlU64 i;

        /* Set PAC count. */
        pacCount = (size_t)(splitTable->splitCount + 1);

        /* Allocate total PAC entries array. */
        pacs = (HlBlob**)hlAlloc((sizeof(HlBlob*) * pacCount) +
            (sizeof(HlBlob) * splitTable->splitCount));

        if (!pacs)
        {
            result = HL_ERROR_OUT_OF_MEMORY;
            goto end;
        }

        /* Set root PAC blob pointer. */
        pacs[0] = &root;

        /* Setup split PAC blob pointers. */
        {
            HlBlob* splitBlobs = (HlBlob*)&pacs[pacCount];
            for (i = 0; i < splitTable->splitCount; ++i)
            {
                pacs[i + 1] = &splitBlobs[i];
            }
        }

        /* Setup split PAC entries, decompressing if necessary. */
        for (i = 0; i < splitTable->splitCount; ++i)
        {
            const HlPACxV4Chunk* chunks = (const HlPACxV4Chunk*)
                hlOff64Get(&splitEntries[i].chunksOffset);

            result = hlINPACxV4DecompressAndFix(HL_ADD_OFF(header,
                splitEntries[i].offset), chunks, splitEntries[i].chunkCount,
                splitEntries[i].compressedSize, splitEntries[i].uncompressedSize,
                pacs[i + 1]);

            if (HL_FAILED(result)) goto end;
        }

        /* Generate HlArchive. */
        result = hlPACxV3Read(pacs, pacCount, archive);
    }
    else
    {
        /* Generate HlArchive. */
        HlBlob* tmpRootBlobPtr = &root;
        result = hlPACxV3Read((const HlBlob**)(&tmpRootBlobPtr), 1, archive);
    }

end:
    /* Free PAC entries and PAC data. */
    if (pacs)
    {
        HlU32 i;
        for (i = 0; i < pacCount; ++i)
        {
            if (pacs[i]->size) hlFree(pacs[i]->data);
        }

        hlFree(pacs);
    }
    else
    {
        if (root.size) hlFree(root.data);
    }

    return result;
}

HlResult hlPACxV4Load(const HlNChar* HL_RESTRICT filePath,
    HlBool loadSplits, HlArchive** HL_RESTRICT archive)
{
    HlBlob* blob;
    HlResult result;

    /* Load archive. */
    result = hlBlobLoad(filePath, &blob);
    if (HL_FAILED(result)) return result;

    /* Fix PACxV4 data. */
    hlPACxV4Fix(blob);

    /* Parse blob into HlArchive, free blob, and return. */
    result = hlPACxV4Read(blob, loadSplits, archive);
    hlFree(blob);
    return result;
}

#ifndef HL_NO_EXTERNAL_WRAPPERS
HlPACxV2NodeTree* hlPACxV2DataGetTypeTreeExt(HlPACxV2BlockDataHeader* dataBlock)
{
    return hlPACxV2DataGetTypeTree(dataBlock);
}

HlPACxV2DataEntry* hlPACxV2DataGetDataEntriesExt(HlPACxV2BlockDataHeader* dataBlock)
{
    return hlPACxV2DataGetDataEntries(dataBlock);
}

HlPACxV2ProxyEntryTable* hlPACxV2DataGetProxyEntryTableExt(
    HlPACxV2BlockDataHeader* dataBlock)
{
    return hlPACxV2DataGetProxyEntryTable(dataBlock);
}

char* hlPACxV2DataGetStringTableExt(HlPACxV2BlockDataHeader* dataBlock)
{
    return hlPACxV2DataGetStringTable(dataBlock);
}

const void* hlPACxV2DataGetOffsetTableExt(const HlPACxV2BlockDataHeader* dataBlock)
{
    return hlPACxV2DataGetOffsetTable(dataBlock);
}

HlPACxV3NodeTree* hlPACxV3GetTypeTreeExt(HlPACxV3Header* header)
{
    return hlPACxV3GetTypeTree(header);
}

HlPACxV3SplitTable* hlPACxV3GetSplitTableExt(HlPACxV3Header* header)
{
    return hlPACxV3GetSplitTable(header);
}

HlPACxV3DataEntry* hlPACxV3GetDataEntriesExt(HlPACxV3Header* header)
{
    return hlPACxV3GetDataEntries(header);
}

char* hlPACxV3GetStringTableExt(HlPACxV3Header* header)
{
    return hlPACxV3GetStringTable(header);
}

void* hlPACxV3GetDataExt(HlPACxV3Header* header)
{
    return hlPACxV3GetData(header);
}

const void* hlPACxV3GetOffsetTableExt(HlPACxV3Header* header)
{
    return hlPACxV3GetOffsetTable(header);
}

HlPACxV4Chunk* hlPACxV4GetRootChunksExt(HlPACxV4Header* header)
{
    return hlPACxV4GetRootChunks(header);
}
#endif