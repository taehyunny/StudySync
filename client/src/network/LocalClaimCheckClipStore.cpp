#include "pch.h"
#include "network/LocalClaimCheckClipStore.h"

#include <sstream>

LocalClaimCheckClipStore::LocalClaimCheckClipStore(std::string clip_directory)
    : clip_directory_(std::move(clip_directory))
{
}

ClipRef LocalClaimCheckClipStore::store_clip(const PostureEvent& event)
{
    std::ostringstream uri;
    uri << clip_directory_ << "/event_" << event.timestamp_ms << ".clip";

    // TODO: encode event.frames as JPEG sequence or MP4 and write to uri.
    ClipRef ref;
    ref.uri = uri.str();
    ref.frame_count = event.frames.size();
    return ref;
}

