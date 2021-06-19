External DSP(Digital Signal Processor) interface for FreeSwitch(TM) voice communications platform
===============================
This module allow you to use any external application to proceed media trafic inside of FreeSwich(TM) channel.
Your application should be able to receive voice stream from STDIN and send voice stream to STDOUT.
This modeule was tested with GStreamer v1.0.

Building as a part of FreeSwitch(TM) source tree
--------------------
To build module as a part of FreeSwitch source tree perform following steps:

- Add it as a ``git submodule add --name kvishnivetsky-mod_external_dsp https://github.com/kvishnivetsky/mod_external_dsp src/mod/applications/mod_external_dsp``
- Add ``src/mod/applications/mod_external_dsp/Makefile`` to ``AC_CONFIG_FILES`` of ``configure.ac``
- Add ``applications/mod_external_dsp`` to ``modules.conf``
- Build entire FreeSwitch project or module only

Configuration
--------------------
No any static configuration is needed.

Usage
--------------------
To start DSP:
- establish a channel, bridged or not does not matter
- run API command ``process_audio <UUID> start <your external aplication with arguments>``

To stop DSP:
- run API command ``process_audio <UUID> stop``

Example
--------------------
PCMU channel and GStreamer as a processor:

``process_audio 48692d63-b8de-41ca-b21d-f27db8484346 start gst-launch-1.0 -q fdsrc ! rawaudioparse use-sink-caps=false format=mulaw sample-rate=8000 num-channels=1 ! mulawdec ! audioconvert ! audioconvert ! mulawenc ! fdsink``
