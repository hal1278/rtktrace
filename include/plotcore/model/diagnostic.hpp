#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "plotcore/model/sample.hpp"

namespace plotcore {

enum class DiagnosticSeverity : std::uint8_t {
    Info,
    Warning,
    RequiresDecision,
    Fatal,
};

enum class DiagnosticAction : std::uint8_t {
    Ignored,
    SampleRemoved,
    SampleReplaced,
    LoadedAsQualityZero,
    UserDecisionRequired,
    FileRejected,
};

enum class DiagnosticCode : std::uint8_t {
    ParseError,
    ChecksumError,
    MissingField,
    InvalidTime,
    TimeReversal,
    DuplicateEpoch,
    UnsupportedTalker,
    MultipleTalkers,
    MissingGeoidSeparation,
    MissingDate,
    DateValidationMismatch,
    UnknownPosQuality,
    UnknownNmeaQuality,
    RateEstimationFailure,
    EmptyEnuReferenceRange,
    NoCommonIntersection,
};

struct Diagnostic {
    DiagnosticSeverity severity;
    DiagnosticCode code;
    std::string file_name;
    std::optional<std::size_t> source_line_number;
    std::optional<GpsTime> time;
    std::string message;
    DiagnosticAction action;
};

} // namespace plotcore
