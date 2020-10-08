/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Goertzel routines are borrowed from Steve Underwood's tremendous work on the
 * DTMF detector.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Convenience Signal Processing routines
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Steve Underwood <steveu@coppice.org>
 */

/*! \li \ref dsp.c uses the configuration file \ref dsp.conf
 * \addtogroup configuration_file Configuration Files
 */

/*!
 * \page dsp.conf dsp.conf
 * \verbinclude dsp.conf.sample
 */

/* Some routines from tone_detect.c by Steven Underwood as published under the zapata library */
/*
	tone_detect.c - General telephony tone detection, and specific
					detection of DTMF.

	Copyright (C) 2001  Steve Underwood <steveu@coppice.org>

	Despite my general liking of the GPL, I place this code in the
	public domain for the benefit of all mankind - even the slimy
	ones who might try to proprietize my work and use it to my
	detriment.
*/

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"

#include <math.h>

#include "asterisk/module.h"
#include "asterisk/frame.h"
#include "asterisk/format_cache.h"
#include "asterisk/channel.h"
#include "asterisk/dsp.h"
#include "asterisk/ulaw.h"
#include "asterisk/alaw.h"
#include "asterisk/utils.h"
#include "asterisk/options.h"
#include "asterisk/config.h"
#include "asterisk/test.h"


/*!
 * \brief The default silence threshold we will use if an alternate
 * configured value is not present or is invalid.
 */
static const int DEFAULT_SILENCE_THRESHOLD = 256;

#define CONFIG_FILE_NAME "dsp.conf"

static int thresholds[THRESHOLD_MAX];

int ast_dsp_busydetect(struct ast_dsp *dsp)
{
	return dsp->tech->dsp_busydetect(dsp->dsp_pvt);
}


int ast_dsp_silence_with_energy(struct ast_dsp *dsp, struct ast_frame *f, int *totalsilence, int *frames_energy)
{
	return dsp->tech->dsp_silence_with_energy(dsp->dsp_pvt, f, totalsilence, frames_energy);
}

int ast_dsp_silence(struct ast_dsp *dsp, struct ast_frame *f, int *totalsilence)
{
	return dsp->tech->dsp_silence(dsp->dsp_pvt, f, totalsilence);
}

int ast_dsp_noise(struct ast_dsp *dsp, struct ast_frame *f, int *totalnoise)
{
	return dsp->tech->dsp_noise(dsp->dsp_pvt, f, totalnoise);
}

struct ast_frame *ast_dsp_process(struct ast_channel *chan, struct ast_dsp *dsp, struct ast_frame *af)
{
	return dsp->tech->dsp_process(chan, dsp->dsp_pvt, af);
}

unsigned int ast_dsp_get_sample_rate(const struct ast_dsp *dsp)
{
	return dsp->tech->dsp_get_sample_rate(dsp->dsp_pvt);
}


void ast_dsp_set_features(struct ast_dsp *dsp, int features)
{
	dsp->tech->dsp_set_features(dsp->dsp_pvt, features);
}


int ast_dsp_get_features(struct ast_dsp *dsp)
{
        return dsp->tech->dsp_get_features(dsp->dsp_pvt);
}


void ast_dsp_free(struct ast_dsp *dsp)
{
	if(dsp) {
		dsp->tech->dsp_free(dsp->dsp_pvt);
		ast_free(dsp);
	}
}

void ast_dsp_set_threshold(struct ast_dsp *dsp, int threshold)
{
	dsp->tech->dsp_set_threshold(dsp->dsp_pvt, threshold);
}

void ast_dsp_set_busy_count(struct ast_dsp *dsp, int cadences)
{
	dsp->tech->dsp_set_busy_count(dsp->dsp_pvt, cadences);
}

void ast_dsp_set_busy_pattern(struct ast_dsp *dsp, const struct ast_dsp_busy_pattern *cadence)
{
	dsp->tech->dsp_set_busy_pattern(dsp->dsp_pvt, cadence);
}

void ast_dsp_digitreset(struct ast_dsp *dsp)
{
	dsp->tech->dsp_digitreset(dsp->dsp_pvt);
}

void ast_dsp_reset(struct ast_dsp *dsp)
{
	dsp->tech->dsp_reset(dsp->dsp_pvt);
}

int ast_dsp_set_digitmode(struct ast_dsp *dsp, int digitmode)
{
	return dsp->tech->dsp_set_digitmode(dsp->dsp_pvt, digitmode);
}

int ast_dsp_set_faxmode(struct ast_dsp *dsp, int faxmode)
{
	return dsp->tech->dsp_set_faxmode(dsp->dsp_pvt, faxmode);
}

int ast_dsp_set_call_progress_zone(struct ast_dsp *dsp, char *zone)
{
	return dsp->tech->dsp_set_call_progress_zone(dsp->dsp_pvt, zone);
}

int ast_dsp_was_muted(struct ast_dsp *dsp)
{
	return dsp->tech->dsp_was_muted(dsp->dsp_pvt);
}

int ast_dsp_get_tstate(struct ast_dsp *dsp)
{
	return dsp->tech->dsp_get_tstate(dsp->dsp_pvt);
}

int ast_dsp_get_tcount(struct ast_dsp *dsp)
{
	return dsp->tech->dsp_get_tcount(dsp->dsp_pvt);
}

static int _dsp_init(int reload)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	int cfg_threshold;

	if ((cfg = ast_config_load2(CONFIG_FILE_NAME, "dsp", config_flags)) == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	}

	thresholds[THRESHOLD_SILENCE] = DEFAULT_SILENCE_THRESHOLD;

	if (cfg == CONFIG_STATUS_FILEMISSING || cfg == CONFIG_STATUS_FILEINVALID) {
		return 0;
	}

	for (v = ast_variable_browse(cfg, "default"); v; v = v->next) {
		if (!strcasecmp(v->name, "silencethreshold")) {
			if (sscanf(v->value, "%30d", &cfg_threshold) < 1) {
				ast_log(LOG_WARNING, "Unable to convert '%s' to a numeric value.\n", v->value);
			} else if (cfg_threshold < 0) {
				ast_log(LOG_WARNING, "Invalid silence threshold '%d' specified, using default\n", cfg_threshold);
			} else {
				thresholds[THRESHOLD_SILENCE] = cfg_threshold;
			}
		}
	}
	ast_config_destroy(cfg);

	return 0;
}

int ast_dsp_get_threshold_from_settings(enum threshold which)
{
	return thresholds[which];
}

static int unload_module(void)
{

	return 0;
}

static int load_module(void)
{
	if (_dsp_init(0)) {
		return AST_MODULE_LOAD_FAILURE;
	}

	return AST_MODULE_LOAD_SUCCESS;
}

static int reload_module(void)
{
	return _dsp_init(1);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "DSP",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CORE,
	.requires = "extconfig",
);
