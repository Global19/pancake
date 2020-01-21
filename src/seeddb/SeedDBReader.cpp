// Authors: Ivan Sovic

#include <pacbio/seeddb/SeedDBReader.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace PacBio {
namespace Pancake {

SeedDBReader::SeedDBReader(std::shared_ptr<PacBio::Pancake::SeedDBIndexCache>& seedDBCache)
    : seedDBIndexCache_(seedDBCache)
{
    // Sanity check.
    if (seedDBIndexCache_->fileLines.empty())
        throw std::runtime_error("There are no file specifications in the input index file.");
    if (seedDBIndexCache_->seqLines.empty())
        throw std::runtime_error("There are no sequences in the input index file.");
}

SeedDBReader::~SeedDBReader() = default;

bool SeedDBReader::GetSeedsForSequence(SequenceSeeds& record, int64_t seqId)
{
    // Clear the output.
    record.Seeds(std::vector<__int128>());
    record.Name("");
    record.Id(-1);

    // Sanity check for the sequence ID.
    if (seqId < 0 || seqId >= static_cast<int32_t>(seedDBIndexCache_->seqLines.size()))
        throw std::runtime_error("Invalid seqId out of bounds (SeedDBReader). seqId = " +
                                 std::to_string(seqId));

    // Find the sequence.
    auto it = seedDBIndexCache_->seqIdToOrdinalId.find(seqId);
    if (it == seedDBIndexCache_->seqIdToOrdinalId.end())
        throw std::runtime_error("Could not find seqId in the index (SeedDBReader). seqId = " +
                                 std::to_string(seqId));
    int32_t ordinalId = it->second;

    // Access the SequenceLine object.
    const auto& sl = seedDBIndexCache_->seqLines[ordinalId];

    // Load the data and update fileHandler_->nextOrdinalId.
    LoadSeedsForSequence_(record, fileHandler_, seedDBIndexCache_->fileLines,
                          seedDBIndexCache_->indexParentFolder, sl, ordinalId);

    return true;
}
bool SeedDBReader::GetSeedsForSequence(SequenceSeeds& record, const std::string& seqName)
{
    // Clear the output.
    record.Seeds(std::vector<__int128>());
    record.Name("");
    record.Id(-1);

    // Find the sequence.
    auto it = seedDBIndexCache_->headerToOrdinalId.find(seqName);
    if (it == seedDBIndexCache_->headerToOrdinalId.end())
        throw std::runtime_error(
            "Invalid sequence name, it does not exist in the SeedDB. seqName = " + seqName);

    int32_t ordinalId = it->second;

    // Access the SequenceLine object.
    const auto& sl = seedDBIndexCache_->seqLines[ordinalId];

    // Load the data and update fileHandler_->nextOrdinalId.
    LoadSeedsForSequence_(record, fileHandler_, seedDBIndexCache_->fileLines,
                          seedDBIndexCache_->indexParentFolder, sl, ordinalId);

    return true;
}
bool SeedDBReader::GetNext(SequenceSeeds& record)
{
    // Clear the output.
    record.Seeds(std::vector<__int128>());
    record.Name("");
    record.Id(-1);

    // Sanity check for the sequence ID.
    if (fileHandler_.nextOrdinalId < 0) throw std::runtime_error("Invalid nextSeqId < 0.");

    // Can't go to the next sequence, we loaded all of them.
    if (fileHandler_.nextOrdinalId >= static_cast<int32_t>(seedDBIndexCache_->seqLines.size()))
        return false;

    // Access the SequenceLine object.
    const auto& sl = seedDBIndexCache_->seqLines[fileHandler_.nextOrdinalId];

    // Load the data and update fileHandler_->nextOrdinalId.
    LoadSeedsForSequence_(record, fileHandler_, seedDBIndexCache_->fileLines,
                          seedDBIndexCache_->indexParentFolder, sl, fileHandler_.nextOrdinalId);

    return true;
}
bool SeedDBReader::GetNextBatch(std::vector<SequenceSeeds>& records, int64_t batchSize)
{
    records.clear();

    // Sanity check for the sequence ID.
    if (fileHandler_.nextOrdinalId < 0)
        throw std::runtime_error("Invalid nextSeqId < 0 (SeedDBReader).");

    // Batch size < 0 loads everything as one batch.
    batchSize = (batchSize < 0) ? std::numeric_limits<int64_t>::max() : batchSize;

    int32_t numSeqLines = seedDBIndexCache_->seqLines.size();
    int64_t loadedBytes = 0;
    while (fileHandler_.nextOrdinalId < numSeqLines) {
        // Access the SequenceLine object.
        const auto& sl = seedDBIndexCache_->seqLines[fileHandler_.nextOrdinalId];

        // Load the data and decompress if required.
        Pancake::SequenceSeeds record;

        // Load the data and update fileHandler_->nextOrdinalId.
        LoadSeedsForSequence_(record, fileHandler_, seedDBIndexCache_->fileLines,
                              seedDBIndexCache_->indexParentFolder, sl, fileHandler_.nextOrdinalId);

        // Store the record and increase the counters.
        records.emplace_back(record);
        loadedBytes += static_cast<int64_t>(record.Seeds().size() * 16);

        if (loadedBytes >= batchSize) break;
    }

    if (records.empty()) return false;

    return true;
}
bool SeedDBReader::GetBlock(std::vector<SequenceSeeds>& records, int32_t blockId)
{
    records.clear();

    // Sanity check for the sequence ID.
    if (blockId < 0 || blockId >= static_cast<int32_t>(seedDBIndexCache_->blockLines.size())) {
        std::ostringstream oss;
        oss << "Invalid blockId (SeedDBReader). blockId = " << blockId
            << ", blocks.size() = " << seedDBIndexCache_->blockLines.size();
        throw std::runtime_error(oss.str());
    }

    const auto& block = seedDBIndexCache_->blockLines[blockId];

    // Sanity check that the block's range is good.
    int32_t numSeedLines = seedDBIndexCache_->seqLines.size();
    if (block.startSeqId < 0 || block.endSeqId <= block.startSeqId ||
        block.startSeqId >= numSeedLines || block.endSeqId > numSeedLines) {
        std::ostringstream oss;
        oss << "Invalid block values: start = " << block.startSeqId << ", end = " << block.endSeqId
            << ", numSeedLines = " << numSeedLines << " (SeedDBReader).";
        throw std::runtime_error(oss.str());
    }

    fileHandler_.nextOrdinalId = block.startSeqId;

    while (fileHandler_.nextOrdinalId < block.endSeqId) {
        // Access the SequenceLine object.
        const auto& sl = seedDBIndexCache_->seqLines[fileHandler_.nextOrdinalId];

        // Load the data and decompress if required.
        Pancake::SequenceSeeds record;
        // Load the data and update fileHandler_->nextOrdinalId.
        LoadSeedsForSequence_(record, fileHandler_, seedDBIndexCache_->fileLines,
                              seedDBIndexCache_->indexParentFolder, sl, fileHandler_.nextOrdinalId);

        // Store the record.
        records.emplace_back(std::move(record));
    }

    if (records.empty()) return false;

    return true;
}
bool SeedDBReader::JumpTo(int64_t seqId)
{
    // Sanity check for the sequence ID.
    if (seqId < 0 || seqId >= static_cast<int32_t>(seedDBIndexCache_->seqLines.size()))
        throw std::runtime_error(
            "Cannot JumpTo, invalid seqId out of bounds (SeedDBReader). seqId = " +
            std::to_string(seqId));

    // Find the sequence.
    auto it = seedDBIndexCache_->seqIdToOrdinalId.find(seqId);
    if (it == seedDBIndexCache_->seqIdToOrdinalId.end())
        throw std::runtime_error("Could not find seqId in the index. seqId = " +
                                 std::to_string(seqId));
    int32_t ordinalId = it->second;

    // Access the SequenceLine object.
    const auto& sl = seedDBIndexCache_->seqLines[ordinalId];

    // Jump to the correct file and offset, and update the fileHandler_->nextOrdinalId.
    AccessLocation_(fileHandler_, seedDBIndexCache_->fileLines,
                    seedDBIndexCache_->indexParentFolder, sl.fileId, ordinalId, sl.fileOffset);

    return true;
}
bool SeedDBReader::JumpTo(const std::string& seqName)
{
    // Find the sequence.
    auto it = seedDBIndexCache_->headerToOrdinalId.find(seqName);
    if (it == seedDBIndexCache_->headerToOrdinalId.end())
        throw std::runtime_error(
            "Invalid sequence name, it does not exist in the SeedDB. seqName = " + seqName);
    int32_t ordinalId = it->second;

    // Access the SequenceLine object.
    const auto& sl = seedDBIndexCache_->seqLines[ordinalId];

    // Jump to the correct file and offset, and update the fileHandler_->nextOrdinalId.
    AccessLocation_(fileHandler_, seedDBIndexCache_->fileLines,
                    seedDBIndexCache_->indexParentFolder, sl.fileId, ordinalId, sl.fileOffset);

    return true;
}

void SeedDBReader::AccessLocation_(OpenFileHandler& fileHandler,
                                   const std::vector<PacBio::Pancake::SeedDBFileLine>& fileLines,
                                   const std::string& indexParentFolder, int32_t fileId,
                                   int32_t nextOrdinalId, int64_t offset) const
{
    if (fileId < 0 || fileId >= static_cast<int32_t>(fileLines.size()))
        throw std::runtime_error("Invalid fileId value (SeedDBReader): " + std::to_string(fileId));
    if (offset < 0)
        throw std::runtime_error("Invalid file offset value (SeedDBReader): " +
                                 std::to_string(offset));

    const auto& fl = fileLines[fileId];

    if (fileHandler.fileId != fileId) {
        std::string actualPath = JoinPath(indexParentFolder, fl.filename);
        fileHandler.fp = PacBio::Pancake::OpenFile(actualPath.c_str(), "rb");
        fileHandler.fileId = fileId;
        fileHandler.pos = 0;
    }
    if (offset != fileHandler.pos) {
        int32_t rv = fseek(fileHandler.fp.get(), offset, SEEK_SET);
        if (rv)
            throw std::runtime_error("Could not fseek to position (SeedDBReader): " +
                                     std::to_string(offset));
        fileHandler.pos = offset;
    }
    fileHandler.nextOrdinalId = nextOrdinalId;
}

void SeedDBReader::LoadSeedsForSequence_(
    Pancake::SequenceSeeds& record, OpenFileHandler& fileHandler,
    const std::vector<PacBio::Pancake::SeedDBFileLine>& fileLines,
    const std::string& indexParentFolder, const SeedDBSeedsLine& sl, int32_t ordinalId) const
{

    // Jump to the correct file and offset.
    AccessLocation_(fileHandler, fileLines, indexParentFolder, sl.fileId, ordinalId + 1,
                    sl.fileOffset);

    std::vector<__int128> seeds(sl.numSeeds);
    int64_t n = fread(&seeds[0], sizeof(__int128), sl.numSeeds, fileHandler.fp.get());
    if (n != sl.numSeeds || n * 16 != sl.numBytes) {
        std::ostringstream oss;
        oss << "Could not read seeds for sequence '" << sl.header
            << "'. Num bytes read: " << n * sizeof(__int128) << ", expected: " << sl.numBytes
            << "; num seeds read: " << n << ", expected: " << sl.numSeeds << ". (SeedDBReader)";
        throw std::runtime_error(oss.str());
    }
    record.Name(sl.header);
    record.Seeds(std::move(seeds));
    record.Id(sl.seqId);
}

}  // namespace Pancake
}  // namespace PacBio
