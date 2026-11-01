#pragma once

enum class TimingCheckType {
    SETUP,
    HOLD,
    WIDTH,
    RECOVERY
};

enum class TimingCheckEdge {
    ANY,
    POSEDGE,
    NEGEDGE
};
