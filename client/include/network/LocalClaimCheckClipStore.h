#pragma once

#include "network/IEventClipStore.h"

#include <string>

class LocalClaimCheckClipStore final : public IEventClipStore {
public:
    explicit LocalClaimCheckClipStore(std::string clip_directory);

    ClipRef store_clip(const PostureEvent& event) override;

private:
    std::string clip_directory_;
};

