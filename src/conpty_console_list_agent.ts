/**
 * Copyright (c) 2019, Microsoft Corporation (MIT License).
 *
 * This module fetches the console process list for a particular PID. It must be
 * called from a different process (child_process.fork) as there can only be a
 * single console attached to a process.
 */

let getConsoleProcessList: any;
try {
  getConsoleProcessList = require(`../bin/${process.platform}-${process.arch}/conpty_console_list.node`).getConsoleProcessList;
} catch (outerError) {
  try {
    getConsoleProcessList = require('../build/Debug/conpty_console_list.node').getConsoleProcessList;
  }
  catch (innerError) {
    console.error('innerError', innerError);
    // Re-throw the exception from the Release require if the Debug require fails as well
    throw outerError;
  }
}

const shellPid = parseInt(process.argv[2], 10);
const consoleProcessList = getConsoleProcessList(shellPid);
process.send({ consoleProcessList });
process.exit(0);
