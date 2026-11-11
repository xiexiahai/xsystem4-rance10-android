/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XSYSTEM4_RANCE10_DIAGNOSTICS_H
#define XSYSTEM4_RANCE10_DIAGNOSTICS_H

#include <stdbool.h>
#include <SDL.h>

/* Presence alone is not an enabled flag: configuration may explicitly say 0.
 * SDL_getenv also uses the same environment as Android's nativeSetenv. */
static inline bool r10_diagnostic_enabled(const char *name)
{
	const char *value = SDL_getenv(name);
	return value && (!SDL_strcasecmp(value, "1") ||
		!SDL_strcasecmp(value, "true") || !SDL_strcasecmp(value, "on"));
}

#endif
