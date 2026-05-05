#pragma once

#include "network/IEventClipStore.h"

#include <cstdint>
#include <string>

class LocalClaimCheckClipStore final : public IEventClipStore {
public:
    LocalClaimCheckClipStore(std::string clip_directory, std::uint32_t retention_days = 3);

    ClipRef store_clip(const PostureEvent& event) override;

private:
    std::string clip_directory_;
    std::uint32_t retention_days_ = 3;
};
