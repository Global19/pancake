// Author: Ivan Sovic

#ifndef PANCAKE_OVERLAPHIFI_SEEDHIT_H
#define PANCAKE_OVERLAPHIFI_SEEDHIT_H

#include <pacbio/util/CommonTypes.h>
#include <cstdint>

namespace PacBio {
namespace Pancake {

static const PacBio::Pancake::Int128t MASK128_LOW32bit = 0x000000000FFFFFFFF;

class SeedHit
{
public:
    SeedHit() = default;
    ~SeedHit() = default;
    PacBio::Pancake::Int128t PackTo128() const
    {
        PacBio::Pancake::Int128t ret = 0;
        ret = ((static_cast<PacBio::Pancake::Int128t>(targetId) & MASK128_LOW32bit) << 97) |
              ((static_cast<PacBio::Pancake::Int128t>(targetRev) & MASK128_LOW32bit) << 96) |
              ((static_cast<PacBio::Pancake::Int128t>(targetPos) & MASK128_LOW32bit) << 64) |
              ((static_cast<PacBio::Pancake::Int128t>(flags) & MASK128_LOW32bit) << 32) |
              (static_cast<PacBio::Pancake::Int128t>(queryPos) & MASK128_LOW32bit);
        return ret;
    }
    bool operator<(const SeedHit& b) const { return this->PackTo128() < b.PackTo128(); }
    bool operator==(const SeedHit& b) const
    {
        return targetId == b.targetId && targetRev == b.targetRev && targetPos == b.targetPos &&
               flags == b.flags && queryPos == b.queryPos;
    }
    int32_t Diagonal() const { return targetPos - queryPos; }

    int32_t targetId : 31;
    bool targetRev : 1;
    int32_t targetPos : 32;
    int32_t flags : 32;
    int32_t queryPos : 32;
};

}  // namespace Pancake
}  // namespace PacBio

#endif  // PANCAKE_OVERLAPHIFI_SEEDHIT_H