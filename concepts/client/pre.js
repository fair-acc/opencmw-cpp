var serverOnExit = null;
var http = null;

function isPortInUse(port) {
    return new Promise((resolve) => {
        const server = http.createServer();
        server.on('error', () => {
            resolve(true);
        });
        server.on('listening', () => {
            server.close();
            resolve(false);
        });
        server.listen(port);
    });
}

// for nodejs - load implementation for xmlhttprequest
if (typeof XMLHttpRequest === 'undefined') {

    /*global XMLHttpRequest:writable*/
    XMLHttpRequest = require('xhr2');

// only available on nodejs, if we don't have a server to run against, start one
    http = require('http');
    const {exit} = require('process');

    const PORT = 8053;

// Check if port 8055 is in use
    new Promise((resolve) => {
        const server = http.createServer();
        server.on('error', () => {
            resolve(true);
        });
        server.on('listening', () => {
            server.close();
            resolve(false);
        });
        server.listen(PORT);
    }).then((inUse) => {
            if (!inUse) {
                // Start the HTTP server
                const server = http.createServer((req, res) => {
                    // Set the Content-Type header to indicate binary data
                    res.setHeader('Content-Type', 'application/octet-stream');

                    // Set the additional headers
                    res.setHeader('Keep-Alive', 'timeout=5, max=5');
                    res.setHeader('x-opencmw-service-name', 'dns');
                    res.setHeader('x-opencmw-topic', '//s?signal_type=&signal_unit=&signal_name=&service_type=&service_name=&port=-1&hostname=&protocol=&signal_rate=nan&contextType=application%2Foctet-stream');

                    // Send the binary response
                    const binaryData = Buffer.from('ffffffff040000005961530001000000602f0da036000000e4010000250000006f70656e636d773a3a736572766963653a3a646e733a3a466c6174456e7472794c69737400ff988e0ac51a000000150000000900000070726f746f636f6c00010000000100000001000000050000006874747000ff335c21ee1a0000002100000009000000686f73746e616d650001000000010000000100000011000000746573742e6578616d706c652e636f6d006881983400160000001000000005000000706f72740001000000010000000100000039050000ffd55573151e000000150000000d000000736572766963655f6e616d6500010000000100000001000000050000007465737400ff846a76151e000000110000000d000000736572766963655f74797065000100000001000000010000000100000000ffc2ad1b281d000000160000000c0000007369676e616c5f6e616d650001000000010000000100000005000000746573743100ffbb0c1f281d000000110000000c0000007369676e616c5f756e69740001000000010000000100000001000000006a17801d281d000000100000000c0000007369676e616c5f726174650001000000010000000100000000007a44ff71c21e281d000000110000000c0000007369676e616c5f74797065000100000001000000010000000100000000fe602f0da03600000000000000250000006f70656e636d773a3a736572766963653a3a646e733a3a466c6174456e7472794c69737400', 'hex');
                    res.end(binaryData);
                });

                server.listen(PORT, () => {
                    console.log(`js http server started on port ${PORT}.`);
                });

                // Register the shutdown callback for Emscripten
                serverOnExit = () => {
                    server.close(() => {
                    });
                };
                addOnExit(serverOnExit);
            }
        })
        .catch((err) => {
            console.error('Error occurred:', err);
        });
}
