/*
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2014, Anthony Minessale II <anthm@freeswitch.org>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Anthony Minessale II <anthm@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Anthony Minessale II <anthm@freeswitch.org>
 *
 * mod_external_dsp.cpp -- Example of writeable media bugs
 *
 */

#include <stdexcept>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <poll.h>

using namespace std;

#include <switch.h>

SWITCH_MODULE_LOAD_FUNCTION(mod_external_dsp_load);
SWITCH_MODULE_DEFINITION(mod_external_dsp, mod_external_dsp_load, NULL, NULL);

struct external_dsp_data {
	switch_core_session_t *session;
	int sample_rate;
	int number_of_channels;
	pid_t pid;
	struct pollfd PFD[2];
	int inpipefd[2];
	int outpipefd[2];
	int errpipefd[2];
	char *dsp_app;
	char **dsp_app_argv;
};

static switch_bool_t external_dsp_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
	struct external_dsp_data *edh = (struct external_dsp_data *) user_data;
	assert(edh != NULL);

	switch (type) {
	case SWITCH_ABC_TYPE_INIT:
		{
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(edh->session), SWITCH_LOG_DEBUG, "Initializing External DSP as %d\n", getpid());

			switch_codec_t *read_codec = switch_core_session_get_read_codec(edh->session);
			// Here we initialize all we need BEGIN
			// Additional initialization stuff
			edh->sample_rate = read_codec->implementation->samples_per_second;
			edh->number_of_channels = read_codec->implementation->number_of_channels;

			pipe(edh->inpipefd);
			pipe(edh->outpipefd);
			pipe(edh->errpipefd);

			edh->pid = fork();
			if (edh->pid == 0)
			{
			    // Child
			    dup2(edh->inpipefd[0],   STDIN_FILENO);
			    dup2(edh->outpipefd[1], STDOUT_FILENO);
			    dup2(edh->errpipefd[1], STDERR_FILENO);

			    //ask kernel to deliver SIGTERM in case the parent dies
			    prctl(PR_SET_PDEATHSIG, SIGTERM);
			    prctl(PR_SET_PDEATHSIG, SIGKILL);

			    // Here we run out DSP process, wich never return normally
			    execvp(edh->dsp_app, edh->dsp_app_argv);

			    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(edh->session), SWITCH_LOG_ERROR, "Failed to run External DSP!\n");
			    // Report an error and exit with code 1
			    exit(1);
			} else {
			    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(edh->session), SWITCH_LOG_DEBUG, "Started External DSP %s PID=%d\n", edh->dsp_app, edh->pid);
			}

			// Close unused parent pipe ends
			close(edh->inpipefd[0]);
			close(edh->outpipefd[1]); edh->PFD[0].fd = edh->outpipefd[0]; edh->PFD[0].events = POLLIN;
			close(edh->errpipefd[1]); edh->PFD[1].fd = edh->errpipefd[1]; edh->PFD[1].events = POLLIN;

			// Here we initialize all we need END
		}
		break;
	case SWITCH_ABC_TYPE_CLOSE:
		{
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(edh->session), SWITCH_LOG_DEBUG, "Destroying External DSP as %d\n", getpid());
			int status = -1;
			// Here we uninitialize all we need BEGIN
			// Additional uninitialization stuff
			close(edh->inpipefd[1]);
			close(edh->outpipefd[0]);
			close(edh->errpipefd[0]);

			kill(edh->pid, SIGKILL); //send SIGKILL signal to the child process
			waitpid(edh->pid, &status, 0);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(edh->session), SWITCH_LOG_DEBUG, "Stopped External DSP %s PID=%d with status=%d\n", edh->dsp_app, edh->pid, status);
			// Here we uninitialize all we need END
		}
		break;
	case SWITCH_ABC_TYPE_READ:
	case SWITCH_ABC_TYPE_WRITE:
		break;
	case SWITCH_ABC_TYPE_READ_REPLACE:
	case SWITCH_ABC_TYPE_WRITE_REPLACE:
		{
			switch_frame_t *frame = NULL;

			// Take farame from channel
			frame = switch_core_media_bug_get_read_replace_frame(bug);

			if (frame != NULL && frame->data != NULL) {
			    if (edh->inpipefd[1] > -1) {
				// Write frame to external app
				int result = write(edh->inpipefd[1], frame->data, frame->datalen);
				if (result < 0) {
				    if (errno == EPIPE) {
					switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(edh->session), SWITCH_LOG_ERROR, "%s: lost connection to DSP, remove media bug.\n", edh->dsp_app);
					close(edh->inpipefd[1]); edh->inpipefd[1] = -1;
					// TODO: restart DSP application

					int status = -1;
					// Here we uninitialize all we need BEGIN
					// Additional uninitialization stuff
					close(edh->inpipefd[1]);
					close(edh->outpipefd[0]);
					close(edh->errpipefd[0]);

					kill(edh->pid, SIGKILL); //send SIGKILL signal to the child process
					waitpid(edh->pid, &status, 0);
					switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(edh->session), SWITCH_LOG_DEBUG, "Stopped External DSP %s PID=%d with status=%d\n", edh->dsp_app, edh->pid, status);
				    }
				}
			    }
			} else {
			    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(edh->session), SWITCH_LOG_DEBUG, "%s: got no frame from channel\n", edh->dsp_app);
			}

			// Here we substitute frames BEGIN
			int ready = poll(edh->PFD, 2, 0);
			if (ready > 0) {
			    if (frame != NULL && frame->data != NULL) {
				if (edh->PFD[0].revents & POLLIN) {
				    frame->datalen = read(edh->PFD[0].fd, frame->data, 320);
				}
			    }
			    if (edh->PFD[1].revents & POLLIN) {
				char buf[1024];memset(buf,0,sizeof(buf));
				int n = read(edh->PFD[1].fd, buf, sizeof(buf));
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(edh->session), SWITCH_LOG_DEBUG, "%s: %s\n", edh->dsp_app, buf);
			    }
			} else {
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(edh->session), SWITCH_LOG_DEBUG, "%s: returned nothing\n", edh->dsp_app);
			}
			// Here we substitute frames END

			// Give farame into channel
			switch_core_media_bug_set_read_replace_frame(bug, frame);
		}
	default:
		break;
	}

	return SWITCH_TRUE;
}

SWITCH_STANDARD_APP(process_audio_start_function)
{
	switch_media_bug_t *bug;
	switch_status_t status;
	switch_channel_t *channel = switch_core_session_get_channel(session);
	struct external_dsp_data *edh;
	char *argv[96];
	int argc;
	char *lbuf = NULL;
	int x, n;

	if ((bug = (switch_media_bug_t *) switch_channel_get_private(channel, "_external_dsp_"))) {
		if (!zstr(data) && !strcasecmp(data, "stop")) {
			switch_channel_set_private(channel, "_external_dsp_", NULL);
			switch_core_media_bug_remove(session, &bug);
		} else {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING, "Cannot run 2 times at once on the same channel!\n");
		}
		return;
	}

	edh = (struct external_dsp_data *) switch_core_session_alloc(session, sizeof(*edh));
	assert(edh != NULL);
	memset(edh,0,sizeof(*edh));

	if (data && (lbuf = switch_core_session_strdup(session, data))
		&& (argc = switch_separate_string(lbuf, ' ', argv, (sizeof(argv) / sizeof(argv[0]))))) {
		n = 0;
		for (x = 0; x < argc; x++) {
		    // Parse argumants
		}
	}

	// Validate argeuments if any

	edh->session = session;

	if ((status = switch_core_media_bug_add(session, "external_dsp", NULL, external_dsp_callback, edh, 0,
											SMBF_READ_REPLACE, &bug)) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Failure!\n");
		return;
	}

	switch_channel_set_private(channel, "_external_dsp_", bug);

}

/* API Interface Function */
#define EXTERNAL_DSP_API_SYNTAX "<uuid> [start|stop] full_path_to_app"
SWITCH_STANDARD_API(process_audio_api_function)
{
	switch_core_session_t *rsession = NULL;
	switch_channel_t *channel = NULL;
        switch_media_bug_t *bug;
        switch_status_t status;
        struct external_dsp_data *edh;
	char *mycmd = NULL;
        int argc = 0;
        char *argv[96] = { 0 };
	char *uuid = NULL;
	char *action = NULL;
	char *lbuf = NULL;
	int x, n;

	if (zstr(cmd)) {
		stream->write_function(stream, "-ERR Empty command!\n");
		goto usage;
	}

        if (!(mycmd = strdup(cmd))) {
		stream->write_function(stream, "-ERR Error while copy command!\n");
                goto usage;
        }

        if ((argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])))) < 2) {
		stream->write_function(stream, "-ERR Too few arguments for a command!\n");
                goto usage;
        }

        uuid = argv[0];
        action = argv[1];

        if (!(rsession = switch_core_session_locate(uuid))) {
                stream->write_function(stream, "-ERR Cannot locate session!\n");
                goto done;
        }

	channel = switch_core_session_get_channel(rsession);

	if ((bug = (switch_media_bug_t *) switch_channel_get_private(channel, "_external_dsp_"))) {
		if (!zstr(action) && !strcasecmp(action, "stop")) {
			switch_channel_set_private(channel, "_external_dsp_", NULL);
			switch_core_media_bug_remove(rsession, &bug);
			stream->write_function(stream, "+OK Success\n");
		} else {
			stream->write_function(stream, "-ERR Cannot run 2 times at once on the same channel!\n");
		}
		goto done;
	}

	if (!zstr(action) && strcasecmp(action, "start")) {
                stream->write_function(stream, "-ERR Unknown action [%s]!\n", action);
		goto usage;
	}

	if (argc < 3) {
		stream->write_function(stream, "-ERR Too few arguments for a command, dsp app missing!\n");
		goto usage;
	}

	edh = (struct external_dsp_data *) switch_core_session_alloc(rsession, sizeof(*edh));
	assert(edh != NULL);
	memset(edh, 0, sizeof(*edh));

	edh->dsp_app = switch_core_session_strdup(rsession, argv[2]);
	edh->dsp_app_argv = (char **) switch_core_session_alloc(rsession, sizeof(char *) * argc);
	n = 0;
	for (x = 2; x < argc; x++) {
	    // Parse arguments
	    edh->dsp_app_argv[x-2] = switch_core_session_strdup(rsession, argv[x]);
	}

	// Validate arguments if any

	edh->session = rsession;

	if ((status = switch_core_media_bug_add(rsession, "external_dsp", NULL, external_dsp_callback, edh, 0,
											SMBF_READ_REPLACE, &bug)) != SWITCH_STATUS_SUCCESS) {
		stream->write_function(stream, "-ERR Failure!\n");
		goto done;
	} else {
		switch_channel_set_private(channel, "_external_dsp_", bug);
		stream->write_function(stream, "+OK Success\n");
		goto done;
	}


  usage:
        stream->write_function(stream, "-USAGE: %s\n", EXTERNAL_DSP_API_SYNTAX);

  done:
        if (rsession) {
                switch_core_session_rwunlock(rsession);
        }

        switch_safe_free(mycmd);
        return SWITCH_STATUS_SUCCESS;
}


SWITCH_MODULE_LOAD_FUNCTION(mod_external_dsp_load)
{
	switch_application_interface_t *app_interface;
	switch_api_interface_t *api_interface;

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	SWITCH_ADD_APP(app_interface, "process_audio_start",
		    "Alter the audio stream with external DSP", "Alter the audio stream with external DSP",
		    process_audio_start_function,
		    "[uuid] ", SAF_NONE);

	SWITCH_ADD_API(api_interface, "process_audio", "process_audio", process_audio_api_function, EXTERNAL_DSP_API_SYNTAX);

	switch_console_set_complete("add process_audio ::console::list_uuid ::[start:stop");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet:
 */
