#pragma once

#include <string>

namespace hd
{
// Starts a fresh, identifiable test journal next to the trainer executable.
void StartDiagnosticSession(const char* version);

// Returns the absolute path displayed by the trainer UI.
std::wstring DiagnosticLogPath();

// Opens the journal in the user's default text editor.
bool OpenDiagnosticLog();

// Appends one wall-clock/tick/PID/thread timestamped line to the journal.
void LogDiagnostic(const char* format, ...);
}
