// ============================================================================
// core/i18n_mark.h
// N_ / NC_: mark a literal for the translation template without translating
// it. For a table entry or a name returned from a switch; the site that SHOWS
// it calls I18n::tr() (core/i18n.h). Kept apart so a pure header can mark its
// names without pulling in the catalogue.
// ============================================================================
#pragma once

#define N_(english) english
#define NC_(context, english) english
