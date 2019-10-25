/**
 * Copyright (c) 2012-2015, Christopher Jeffrey, Peter Sunde (MIT License)
 * Copyright (c) 2016, Daniel Imms (MIT License).
 */

import * as path from 'path';
import { Terminal as BaseTerminal } from './terminal';
import { ITerminal, IPtyOpenOptions, IPtyForkOptions } from './interfaces';
import { ArgvOrCommandLine } from './types';

let Terminal: any;
if (process.platform === 'win32') {
  Terminal = require('./windowsTerminal').WindowsTerminal;
} else {
  Terminal = require('./unixTerminal').UnixTerminal;
}

/**
 * Forks a process as a pseudoterminal.
 * @param file The file to launch.
 * @param args The file's arguments as argv (string[]) or in a pre-escaped
 * CommandLine format (string). Note that the CommandLine option is only
 * available on Windows and is expected to be escaped properly.
 * @param options The options of the terminal.
 * @see CommandLineToArgvW https://msdn.microsoft.com/en-us/library/windows/desktop/bb776391(v=vs.85).aspx
 * @see Parsing C++ Comamnd-Line Arguments https://msdn.microsoft.com/en-us/library/17w5ykft.aspx
 * @see GetCommandLine https://msdn.microsoft.com/en-us/library/windows/desktop/ms683156.aspx
 */
export function spawn(file?: string, args?: ArgvOrCommandLine, opt?: IPtyForkOptions): ITerminal {
  return new Terminal(file, args, opt);
}

/** @deprecated */
export function fork(file?: string, args?: ArgvOrCommandLine, opt?: IPtyForkOptions): ITerminal {
  return new Terminal(file, args, opt);
}

/** @deprecated */
export function createTerminal(file?: string, args?: ArgvOrCommandLine, opt?: IPtyForkOptions): ITerminal {
  return new Terminal(file, args, opt);
}

export function open(options: IPtyOpenOptions): ITerminal {
  return Terminal.open(options);
}

/**
 * Expose the native API when not Windows, note that this is not public API and
 * could be removed at any time.
 */

const NODE_MODULE_VERSION = parseInt(process.versions.modules, 10);
var binPath = `${process.platform}-${process.arch}`;  //N-API Binaries for Node versions 10 and later are in folders like win32-x64/
if (NODE_MODULE_VERSION < 64 || ['darwin-x64','linux-ppc64','linux-x64','win32-x64'].indexOf(binPath) < 0 ){
  binPath += `-${process.versions.modules}`;    //NAN Binaries for Node versions before 10 are in folders like win32-x64-57/.
}

export const native = (process.platform !== 'win32' ? require(path.join('..', 'bin', binPath, 'pty.node')) : null);
