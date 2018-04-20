'use strict'

const fs = require('fs');
const os = require('os');
const path = require('path');
const spawn = require('child_process').spawn;

if (os.type() == "OS400")
  process.exit(0);

// Test for pre-built binary.
var binPath = `${process.platform}-${process.arch}-${process.versions.modules}`;
try {
  
  fs.statSync(path.join(__dirname, '..', 'bin', binPath, 'pty.node'));
  console.log(`${binPath} exists, exiting`);
  
}
catch (error) {
  
  build();
  
}

function build() {
  
  const p = spawn(os.platform() === 'win32' ? 'node-gyp.cmd' : 'node-gyp', ['rebuild'], {
    cwd: path.join(__dirname, '..'),
    stdio: 'inherit'
  });

  p.on('exit', function (code) {
    
    if (code == 0)
      afterBuild();
    process.exit(code);
    
  });

}

function afterBuild() {
  
  try {
    
    fs.mkdirSync(path.join(__dirname, '..', 'bin', binPath));
    
  }
  catch (error) {}
  fs.copyFileSync(
    path.join(__dirname, '..', 'build', 'Release', 'pty.node'),
    path.join(__dirname, '..', 'bin', binPath, 'pty.node')
  );
  if (os.platform() == 'win32') {
    
    fs.copyFileSync(
      path.join(__dirname, '..', 'build', 'Release', 'winpty.dll'),
      path.join(__dirname, '..', 'bin', binPath, 'winpty.dll')
    );
    fs.copyFileSync(
      path.join(__dirname, '..', 'build', 'Release', 'winpty-agent.exe'),
      path.join(__dirname, '..', 'bin', binPath, 'winpty-agent.exe')
    );
  
  }
  
}
