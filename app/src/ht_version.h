// Build identity, in its own header so anything can show it.
//
// These used to live in ht_app_common.h, which pulls in the whole ReXApp
// framework and spdlog - far too much to include just to print a version
// string in the settings panel. Splitting them out keeps ONE source of truth:
// ht_app_common.h includes this rather than defining its own copy, so the
// panel and the app can never disagree about what build this is.

#pragma once

// Internal short name. This also becomes the config filename and the log
// prefix, so it must stay a valid Windows filename - no colons, no spaces.
#define HT_BUILD_TITLE "htrecomp"
#define HT_BUILD_STAMP "revision-9.1.0"

// The name shown on the window. Set explicitly in main.cpp rather than by
// renaming the app, precisely because this one is NOT a legal filename.
#define HT_WINDOW_TITLE "Hydro Thunder Hurricane: Recompiled"
