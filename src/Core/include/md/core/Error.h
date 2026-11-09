#pragma once

#include <stdexcept>
#include <string>

namespace md {

/// Error categories. The numeric values double as CLI exit codes.
enum class ErrorCode : int {
    Generic = 1,
    InvalidArgument = 2,
    UnknownMaterial = 3,
    InvalidPreset = 4,
    Io = 5,
    Cancelled = 6,
};

/// Base exception for all errors raised by the core library.
class Error : public std::runtime_error {
public:
    Error(ErrorCode code, const std::string& message) : std::runtime_error(message), code_(code) {}
    ErrorCode code() const { return code_; }

private:
    ErrorCode code_;
};

/// Thrown when a generation is cancelled through its stop token.
class Cancelled : public Error {
public:
    Cancelled() : Error(ErrorCode::Cancelled, "Generation cancelled") {}
};

} // namespace md
