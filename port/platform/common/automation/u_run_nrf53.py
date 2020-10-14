#!/usr/bin/env python

'''Build/run ubxlib for NRF53 and report results.'''

import os                    # For sep(), getcwd()
from time import time
import u_connection
import u_monitor
import u_report
import u_utils
import u_settings

# Prefix to put at the start of all prints
PROMPT = "u_run_nrf53_"

# Expected location of nRFConnect installation
NRFCONNECT_PATH = u_settings.NRF53_NRFCONNECT_PATH #"C:\\nrfconnect\\v1.3.0"

# The path to the Zephyr batch file that sets up its
# environment variables
ZEPHYR_ENV_CMD = u_settings.NRF53_ZEPHYR_ENV_CMD #NRFCONNECT_PATH + os.sep + "zephyr\\zephyr-env.cmd"

# The path to the git-cmd batch file that sets up the
# git-bash environment variables
GIT_BASH_ENV_CMD = u_settings.NRF53_GIT_BASH_ENV_CMD #NRFCONNECT_PATH + os.sep + "toolchain\\cmd\\env.cmd"

# The list of things to execute jlink.exe
RUN_JLINK = [u_utils.JLINK_PATH] + u_settings.NRF53_RUN_JLINK #

# The directory where the runner build can be found
RUNNER_DIR = u_settings.NRF53_RUNNER_DIR #"port\\platform\\nordic\\nrf53\\sdk\\nrfconnect\\runner"

# The board name to build for
BOARD_NAME = u_settings.NRF53_BOARD_NAME #"nrf5340pdk_nrf5340_cpuapp"

# The name of the output sub-directory
BUILD_SUBDIR = u_settings.NRF53_BUILD_SUBDIR #"build"

# The guard time for this build in seconds,
# noting that it can be quite long when
# many builds are running in parallel.
BUILD_GUARD_TIME_SECONDS = u_settings.NRF53_BUILD_GUARD_TIME_SECONDS #60 * 30

# The guard time waiting for a lock on the HW connection seconds
CONNECTION_LOCK_GUARD_TIME_SECONDS = u_connection.CONNECTION_LOCK_GUARD_TIME_SECONDS

# The guard time waiting for a platform lock in seconds
PLATFORM_LOCK_GUARD_TIME_SECONDS = u_utils.PLATFORM_LOCK_GUARD_TIME_SECONDS

# The download guard time for this build in seconds
DOWNLOAD_GUARD_TIME_SECONDS = u_utils.DOWNLOAD_GUARD_TIME_SECONDS

# The guard time for running tests in seconds
RUN_GUARD_TIME_SECONDS = u_utils.RUN_GUARD_TIME_SECONDS

# The inactivity time for running tests in seconds
RUN_INACTIVITY_TIME_SECONDS = u_utils.RUN_INACTIVITY_TIME_SECONDS

# Table of "where.exe" search paths for tools required to be installed
# plus hints as to how to install the tools and how to read their version
TOOLS_LIST = [{"which_string": "west",
               "hint": "can't find \"west\", the Zephyr build tool"  \
                       " (actually a Python script), please install" \
                       " it by opening a command window with "       \
                       " administrator privileges and then typing:"  \
                       " \"pip install west\" followed by something" \
                       " like: \"pip install -r C:\\nrfconnect"      \
                       "\\v1.3.0\\zephyr\\scripts\\requirements.txt\".",
               "version_switch": "--version"},
              {"which_string": "nrfjprog.exe",
               "hint": "couldn't find the nRFConnect SDK at NRFCONNECT_PATH,"  \
                       " please download the latest version from"              \
                       " https://www.nordicsemi.com/Software-and-Tools/"       \
                       "Development-Tools/nRF-Connect-for-desktop"             \
                       " or override the environment variable NRFCONNECT_PATH" \
                       " to reflect where it is.",
               "version_switch": "--version"},
              {"which_string": u_utils.JLINK_PATH,
               "hint": "can't find the SEGGER tools, please install"         \
                       " the latest version of their JLink tools from"       \
                       " https://www.segger.com/downloads/jlink/JLink_Windows.exe" \
                       " and add them to the path.",
               "version_switch": None}]

def print_env(returned_env, printer, prompt):
    '''Print a dictionary that contains the new environment'''
    printer.string("{}environment will be:".format(prompt))
    if returned_env:
        for key, value in returned_env.items():
            printer.string("{}{}={}".format(prompt, key, value))
    else:
        printer.string("{}EMPTY".format(prompt))

def check_installation(tools_list, printer, prompt):
    '''Check that everything required has been installed'''
    success = True

    # Check for the tools on the path
    printer.string("{}checking tools...".format(prompt))
    for item in tools_list:
        if u_utils.exe_where(item["which_string"], item["hint"],
                             printer, prompt):
            if item["version_switch"]:
                u_utils.exe_version(item["which_string"],
                                    item["version_switch"],
                                    printer, prompt)
        else:
            success = False

    return success

def set_env(printer, prompt):
    '''Run the batch files that set up the environment variables'''
    returned_env = {}
    returned_env1 = {}
    returned_env2 = {}
    count = 0

    # It is possible for the process of extracting
    # the environment variables to fail due to machine
    # loading (see comments against EXE_RUN_QUEUE_WAIT_SECONDS
    # in exe_run) so give this up to three chances to succeed
    while not returned_env1 and (count < 3):
        # set shell to True to keep Jenkins happy
        u_utils.exe_run([ZEPHYR_ENV_CMD], None,
                        printer, prompt, shell_cmd=True,
                        returned_env=returned_env1)
        if not returned_env1:
            printer.string("{}warning: retrying {} to capture"  \
                           " the environment variables...".
                           format(prompt, ZEPHYR_ENV_CMD))
        count += 1
    count = 0
    if returned_env1:
        while not returned_env2 and (count < 3):
            # set shell to True to keep Jenkins happy
            u_utils.exe_run([GIT_BASH_ENV_CMD], None,
                            printer, prompt, shell_cmd=True,
                            returned_env=returned_env2)
            if not returned_env2:
                printer.string("{}warning: retrying {} to capture"  \
                               " the environment variables...".
                               format(prompt, GIT_BASH_ENV_CMD))
            count += 1
        if returned_env2:
            returned_env = {**returned_env1, **returned_env2}

    return returned_env

def download(connection, guard_time_seconds, hex_path, env, printer, prompt):
    '''Download the given hex file to an attached NRF53 board'''
    call_list = []

    # Assemble the call list
    call_list.append("west")
    call_list.append("flash")
    call_list.append("--skip-rebuild")
    call_list.append("--nrf-family=NRF53")
    call_list.append("--hex-file")
    call_list.append(hex_path)
    call_list.append("--erase")
    if connection and "debugger" in connection and connection["debugger"]:
        call_list.append("--snr")
        call_list.append(connection["debugger"])

    # Print what we're gonna do
    tmp = ""
    for item in call_list:
        tmp += " " + item
    printer.string("{}in directory {} calling{}".         \
                   format(prompt, os.getcwd(), tmp))

    # Call it
    return u_utils.exe_run(call_list, guard_time_seconds, printer, prompt,
                           shell_cmd=True, set_env=env)

def build(clean, ubxlib_dir, defines, env, printer, prompt, reporter):
    '''Build using west'''
    call_list = []
    defines_text = ""
    runner_dir = ubxlib_dir + os.sep + RUNNER_DIR
    output_dir = os.getcwd() + os.sep + BUILD_SUBDIR
    hex_file_path = None

    # Put west at the front of the call list
    call_list.append("west")

    # Make it verbose
    call_list.append("-v")
    # Do a build
    call_list.append("build")
    # Board name
    call_list.append("-b")
    call_list.append((BOARD_NAME).replace("\\", "/"))
    # Build products directory
    call_list.append("-d")
    call_list.append((BUILD_SUBDIR).replace("\\", "/"))
    if clean:
        # Clean
        call_list.append("-p")
        call_list.append("always")
    # Now the path to build
    call_list.append((runner_dir).replace("\\", "/"))

    # CCACHE is a pain in the bum: falls over on Windows
    # path length issues randomly and doesn't say where.
    # Since we're generally doing clean builds, disable it
    env["CCACHE_DISABLE"] = "1"

    if defines:
        # Set up the U_FLAGS environment variables
        for idx, define in enumerate(defines):
            if idx == 0:
                defines_text += "-D" + define
            else:
                defines_text += " -D" + define
        printer.string("{}setting environment variables U_FLAGS={}".
                       format(prompt, defines_text))
        env["U_FLAGS"] = defines_text

    # Clear the output folder ourselves as well, just
    # to be completely sure
    if not clean or u_utils.deltree(BUILD_SUBDIR, printer, prompt):
        # Print what we're gonna do
        tmp = ""
        for item in call_list:
            tmp += " " + item
        printer.string("{}in directory {} calling{}".         \
                       format(prompt, os.getcwd(), tmp))

        # Call west to do the build
        # Set shell to keep Jenkins happy
        if u_utils.exe_run(call_list, BUILD_GUARD_TIME_SECONDS,
                           printer, prompt, shell_cmd=True,
                           set_env=env):
            hex_file_path = output_dir + os.sep + "zephyr\\zephyr.hex"
    else:
        reporter.event(u_report.EVENT_TYPE_BUILD,
                       u_report.EVENT_ERROR,
                       "unable to clean build directory")

    return hex_file_path

def run(instance, sdk, connection, connection_lock, platform_lock, clean, defines,
        ubxlib_dir, working_dir, printer, reporter, test_report_handle):
    '''Build/run on NRF53'''
    return_value = -1
    hex_file_path = None
    instance_text = u_utils.get_instance_text(instance)

    # Only one SDK for NRF53
    del sdk

    prompt = PROMPT + instance_text + ": "

    # Print out what we've been told to do
    text = "running NRF53"
    if connection and "debugger" in connection and connection["debugger"]:
        text += ", on JLink debugger serial number " + connection["debugger"]
    if clean:
        text += ", clean build"
    if defines:
        text += ", with #define(s)"
        for idx, define in enumerate(defines):
            if idx == 0:
                text += " \"" + define + "\""
            else:
                text += ", \"" + define + "\""
    if ubxlib_dir:
        text += ", ubxlib directory \"" + ubxlib_dir + "\""
    if working_dir:
        text += ", working directory \"" + working_dir + "\""
    printer.string("{}{}.".format(prompt, text))

    reporter.event(u_report.EVENT_TYPE_BUILD,
                   u_report.EVENT_START,
                   "NRF53")
    # Switch to the working directory
    with u_utils.ChangeDir(working_dir):
        # Check that everything we need is installed
        # and configured
        if check_installation(TOOLS_LIST, printer, prompt):
            # Set up the environment variables for Zephyr
            returned_env = set_env(printer, prompt)
            if returned_env:
                # The west tools need to use the environment
                # configured above.
                print_env(returned_env, printer, prompt)
                # Note that Zephyr brings in its own
                # copy of Unity so there is no need to
                # fetch it here.
                # Do the build
                build_start_time = time()
                hex_file_path = build(clean, ubxlib_dir, defines, returned_env,
                                      printer, prompt, reporter)
                if hex_file_path:
                    # Build succeeded, need to lock a connection to do the download
                    reporter.event(u_report.EVENT_TYPE_BUILD,
                                   u_report.EVENT_PASSED,
                                   "build took {:.0f} second(s)".format(time() -
                                                                        build_start_time))
                    with u_connection.Lock(connection, connection_lock,
                                           CONNECTION_LOCK_GUARD_TIME_SECONDS,
                                           printer, prompt) as locked_connection:
                        if locked_connection:
                            # Do the download
                            # In case NRF53 suffers from the same problem as
                            # NRF52, where doing a download or starting SWO logging
                            # on more than one platform at a time seems to cause
                            # problems (even though it should be tied to the
                            # serial number of the given debugger on that board),
                            # lock the platform for this.  Once we've got the
                            # Telnet session opened with the platform it seems
                            # fine to let other downloads/logging-starts happen.
                            with u_utils.Lock(platform_lock, PLATFORM_LOCK_GUARD_TIME_SECONDS,
                                              "platform", printer, prompt) as locked_platform:
                                if locked_platform:
                                    reporter.event(u_report.EVENT_TYPE_DOWNLOAD,
                                                   u_report.EVENT_START)
                                    if download(connection, DOWNLOAD_GUARD_TIME_SECONDS,
                                                hex_file_path, returned_env, printer, prompt):
                                        reporter.event(u_report.EVENT_TYPE_DOWNLOAD,
                                                       u_report.EVENT_COMPLETE)
                                        # OK to release the platform lock again.
                                        platform_lock.release()
                                        reporter.event(u_report.EVENT_TYPE_TEST,
                                                       u_report.EVENT_START)
                                        # Monitor progress
                                        # Open the COM port to get debug output
                                        serial_handle = u_utils.open_serial(connection["serial_port"],
                                                                            115200,
                                                                            printer,
                                                                            prompt)
                                        if serial_handle is not None:
                                            # Monitor progress
                                            return_value = u_monitor. \
                                                           main(serial_handle,
                                                                u_monitor.CONNECTION_SERIAL,
                                                                RUN_GUARD_TIME_SECONDS,
                                                                RUN_INACTIVITY_TIME_SECONDS,
                                                                instance, printer, reporter,
                                                                test_report_handle)
                                            serial_handle.close()
                                        else:
                                            reporter.event(u_report.EVENT_TYPE_INFRASTRUCTURE,
                                                           u_report.EVENT_FAILED,
                                                           "unable to open serial port " + \
                                                           connection["serial_port"])
                                        if return_value == 0:
                                            reporter.event(u_report.EVENT_TYPE_TEST,
                                                           u_report.EVENT_COMPLETE)
                                        else:
                                            reporter.event(u_report.EVENT_TYPE_TEST,
                                                           u_report.EVENT_FAILED)
                                    else:
                                        reporter.event(u_report.EVENT_TYPE_DOWNLOAD,
                                                       u_report.EVENT_FAILED,
                                                       "check debug log for details")
                                else:
                                    # Release the platform lock again.
                                    platform_lock.release()
                                    reporter.event(u_report.EVENT_TYPE_INFRASTRUCTURE,
                                                   u_report.EVENT_FAILED,
                                                   "unable to lock the platform")
                        else:
                            reporter.event(u_report.EVENT_TYPE_INFRASTRUCTURE,
                                           u_report.EVENT_FAILED,
                                           "unable to lock a connection")
                else:
                    return_value = 1
                    reporter.event(u_report.EVENT_TYPE_BUILD,
                                   u_report.EVENT_FAILED,
                                   "check debug log for details")
            else:
                reporter.event(u_report.EVENT_TYPE_INFRASTRUCTURE,
                               u_report.EVENT_FAILED,
                               "environment setup failed")
        else:
            reporter.event(u_report.EVENT_TYPE_INFRASTRUCTURE,
                           u_report.EVENT_FAILED,
                           "there is a problem with the tools installation for NRF53")

    return return_value
