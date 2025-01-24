/*
code is from https://www.geeksforgeeks.org/drag-and-drop-files-in-electronjs/
*/

document.addEventListener('drop', (event) => {
    event.preventDefault();
    event.stopPropagation();

    for (const f of event.dataTransfer.files) {
        // Using the path attribute to get absolute file path
        console.log('File Path of dragged files: ', f)
        let fr = new FileReader();
        fr.onload = function () {
            document.getElementById('output')
                .textContent = fr.result;
        }
        fr.readAsText(f);
    }
});

document.addEventListener('dragover', (e) => {
    e.preventDefault();
    e.stopPropagation();
});

document.addEventListener('dragenter', (event) => {
    console.log('File is in the Drop Space');
});

document.addEventListener('dragleave', (event) => {
    console.log('File has left the Drop Space');
});

/* code from
https://d3js.org/getting-started
*/