'use strict'

const fs = require('fs');
const os = require('os');
const path = require('path');
const spawn = require('child_process').spawn;

// IBM i uses a different module.
if (os.type() == 'OS400')
  process.exit(0);

// Test for pre-built binary.
const binPath = `${process.platform}-${process.arch}`;
const binNames = process.platform === "win32" ? ["conpty.node", "conpty_console_list.node", "pty.node", "winpty.dll", "winpty-agent.exe"] : ["pty.node"];
try {

  binNames.forEach(binName => {
    fs.statSync(path.join(__dirname, '..', 'bin', binPath, binName));
  });
  console.log(`${binPath} exists, exiting`);

}
catch (error) {

  build();

}

function build() {

  const gypArgs = ['rebuild'];
  if (process.env.NODE_PTY_DEBUG) {
    gypArgs.push('--debug');
  }
  const gypProcess = spawn(os.platform() === 'win32' ? 'node-gyp.cmd' : 'node-gyp', gypArgs, {
    cwd: path.join(__dirname, '..'),
    stdio: 'inherit'
  });
  gypProcess.on('exit', function (code) {
    if (code === 0)
      afterBuild();
    process.exit(code);
  });

}

function afterBuild() {

  try {

    fs.mkdirSync(path.join(__dirname, '..', 'bin', binPath));

  }
  catch (error) {}
  binNames.forEach(binName => {
    fs.copyFileSync(
      path.join(__dirname, '..', 'build', 'Release', binName),
      path.join(__dirname, '..', 'bin', binPath, binName)
    );
  });

}
