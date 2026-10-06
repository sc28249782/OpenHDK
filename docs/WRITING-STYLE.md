# Writing Style Guide

## Purpose

Use this guide for English technical documentation, public API comments, command
help, error messages, release notes, acceptance procedures, and pull request
descriptions.

OpenHDK uses a **Simplified Technical English (STE)-inspired** style. The goal
is clear, testable documentation for developers, maintainers, and contributors
who do not use English as their first language.

This guide does not claim ASD-STE100 compliance. A compliance claim requires a
review against the current official ASD-STE100 standard, including its
dictionary and writing rules.

## Scope and precedence

This guide defines project-wide writing principles. The following documents take
precedence when they define a more specific technical, licensing, or scope rule:

- `docs/SPECIFICATION.md` (normative implemented behavior)
- `docs/ARCHITECTURE.md`
- `docs/ROADMAP.md`
- `docs/DEPENDENCY-POLICY.md`
- `LICENSE`
- `docs/THIRD_PARTY_NOTICES.md`

`ROADMAP.md` describes plans; it does not override implemented behavior in
`SPECIFICATION.md`. Contract references explain that specification and must
be corrected together when descriptions conflict.

Use the definitions in [GLOSSARY.md](GLOSSARY.md). Do not define a conflicting
meaning in another document.

## Core rules

- Write one action or one fact in each sentence.
- Use active voice. State the actor when it is important.
- Start an instruction with a direct verb: `Run`, `Build`, `Test`,
  `Check`, `Stop`, or `Do not`.
- State the target, condition, and expected result.
- Use numbered steps for a sequence. A reader must be able to run each step in
  order.
- Use the same term for the same object, contract, or state. Do not use
  synonyms for technical terms.
- Define an abbreviation at its first use unless it is defined in the glossary
  and the document links to it.
- Use code formatting for commands, paths, CMake options, APIs, data formats,
  status values, versions, and dependency names.
- State units and limits explicitly.
- Use American English spelling unless an external contract requires another
  spelling.

Avoid vague words and phrases, including `normally`, `appropriately`,
`quickly`, `simply`, `etc.`, and `as needed`. Replace them with an
explicit condition or action.

## Scope and status wording

State the current implementation boundary precisely.

Do not describe OpenHDK as a complete user-facing karaoke player. The current
work includes deterministic SMF parsing, event decoding, timeline compilation,
`PlaybackSession` timing, dispatch, diagnostics, runtime channel mixing,
velocity curves, named presets, and linked stereo peak limiting through an
optional FluidSynth/miniaudio adapter. It does not yet provide complete user-facing playback.

Do not describe deferred work as current work. In the current SMF foundation,
the following are outside scope: a Qt migration, KAR/NCN parsing or playback,
HNK/HNK3 compatibility, VST2/VST3 integration, and physical MIDI hardware I/O.

Use these status words only with their defined meaning:

- `planned`: Accepted work that is not implemented.
- `implemented`: Code or documentation exists. This does not prove runtime
  behavior.
- `tested`: A stated test passed in its stated environment.
- `validated`: A defined validation procedure passed.
- `released`: A versioned artifact and its required notices are available.

## Licensing and dependency wording

OpenHDK is GPL-3.0-or-later. Preserve required copyright and attribution when
code is ported from HandyKaraoke or another source.

State excluded dependencies exactly. Do not write only `No BASS`. State that
the project excludes BASS, BASS FX, BASSMIDI, BASSmix, BASS_VST, and their
binaries, import libraries, headers, SDK archives, redistribution scripts,
license text, and API wrappers.

Do not state that a candidate dependency is approved or shipped unless
`docs/DEPENDENCY-POLICY.md` says so. For a dependency change, record its exact
version, source URL, SPDX identifier, linking model, notice requirements,
security/update plan, and test coverage.

## Timing and audio instructions

A timing-related instruction must identify the clock or time unit that it uses.
Use a monotonic playback-session clock for MIDI scheduling. Do not use wall
clock wording when the implementation requires the session clock.

For an audio callback, state the safety boundary directly:

> Do not allocate memory, perform file I/O, parse data, or update the UI in the
> audio callback.

When an instruction changes audio timing, endpoint selection, or soundfont
behavior, include a hardware-free test or an explicit manual Windows validation
step.

## Commands, results, and errors

Put one command in each code block. Explain why a command is required before
the command. State the expected result after the command.

Use error messages that tell the user what happened and what to do next.

| Avoid | Prefer |
| --- | --- |
| `The MIDI file is invalid.` | `The SMF track ends before its declared length. Check the file and load a valid SMF format 0 or 1 file.` |
| `Audio setup failed.` | `The selected output endpoint is unavailable. Run \`--list-devices\`, then select an available endpoint index.` |

## Review checklist

Before merge, check that the text:

- uses the definitions in `docs/GLOSSARY.md`;
- states the exact implementation and scope boundary;
- preserves GPL attribution and dependency-policy requirements;
- does not imply that excluded or deferred components are present;
- includes a test or validation step for timing- or audio-affecting changes;
- identifies the required build, CMake, CTest, or manual Windows validation
  environment; and
- does not claim ASD-STE100 compliance without formal review.

## Reference

- ASD-STE100 Simplified Technical English, current official issue:
  <https://www.asd-ste100.org/>
