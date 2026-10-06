# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 OpenHDK contributors

function(expect_diagnostic_error expected_exit expected_pattern)
  execute_process(
    COMMAND "${OPENHDK_EXE}" --midi-diagnostics ${ARGN}
    RESULT_VARIABLE actual_exit
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error)
  if(NOT actual_exit EQUAL expected_exit)
    message(FATAL_ERROR
      "Expected exit ${expected_exit}, got ${actual_exit}. stdout=[${standard_output}] stderr=[${standard_error}]")
  endif()
  if(NOT standard_error MATCHES "${expected_pattern}")
    message(FATAL_ERROR
      "Expected stderr to match [${expected_pattern}], got [${standard_error}]")
  endif()
endfunction()

expect_diagnostic_error(64 "Unknown or incomplete argument: --midi-diagnostics")
expect_diagnostic_error(64 "Unknown or incomplete argument: --velocity-curve" "${MISSING_INPUT}" --velocity-curve)
expect_diagnostic_error(64 "Velocity curve must be linear, soft, or hard" "${MISSING_INPUT}" --velocity-curve unknown)
foreach(curve linear soft hard)
  expect_diagnostic_error(64 "--midi-diagnostics cannot be combined with playback or device options" "${MISSING_INPUT}" --velocity-curve "${curve}")
endforeach()
expect_diagnostic_error(64 "--midi-diagnostics cannot be combined with playback or device options" "${MISSING_INPUT}" --interactive-mixer)
expect_diagnostic_error(1 "MIDI file could not be read:" "${MISSING_INPUT}")
expect_diagnostic_error(1 "MIDI parse failed at byte 0: expected MThd header chunk" "${MALFORMED_INPUT}")
