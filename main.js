/*
Code from https://www.electronjs.org/docs/latest/tutorial/quick-start
*/

const { app, BrowserWindow } = require('electron')
// include the Node.js 'path' module at the top of your file
const path = require('node:path')

const createWindow = () => {
    const win = new BrowserWindow({
        width: 1200,
        height: 800,
        webPreferences: {
            preload: path.join(__dirname, 'preload.js')
        }
    })
  
    win.loadFile('index.html')

    /* open console */
    win.webContents.openDevTools()
}

app.whenReady().then(() => {
    createWindow()
})