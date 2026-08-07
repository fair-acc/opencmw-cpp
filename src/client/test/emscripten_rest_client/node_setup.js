// Node has no XMLHttpRequest; use xhr2 for Emscripten's Fetch API.
if (typeof XMLHttpRequest === 'undefined') {
    XMLHttpRequest = require('xhr2');

    // xhr2 does not report abort completion, so notify Emscripten to release the fetch keepalive.
    const abort = XMLHttpRequest.prototype.abort;
    XMLHttpRequest.prototype.abort = function () {
        const inFlight = this.readyState > 0 && this.readyState < XMLHttpRequest.DONE;
        abort.call(this);
        if (inFlight) {
            this.readyState = XMLHttpRequest.DONE;
            this.onreadystatechange?.();
        }
    };
}
