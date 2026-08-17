#pragma once

#include <vector>

#include "messages.hpp"

struct EventRecorder
{
    std::vector<OutboundEvent> events;

    void operator()(const OutboundEvent &event) { events.push_back(event); }
};
