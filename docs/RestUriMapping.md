# URI mapping in the REST backend

OpenCMW has a few different components that use URIs
as a serialization mechanism for services, topics, etc.
This is the definition of how each component treats
URIs.

The REST backend maps the URL path with the leading slash stripped
to the Majordomo worker service name,
and uses `SubscriptionContext` query parameter as the request topic for subscriptions and long polling.

For HTTP POST, the message payload is passed via the POST request form data.
There is no topic in this case.

# Request type specification

The REST backend maps HTTP requests to Majordomo requests.
By default, `PUT` and `POST` HTTP requests are mapped to Majordomo's `Set`,
and `GET` HTTP request is mapped to Majordomo's `Get` request.

Use `LongPollingIdx` on a GET request to read notifications for a topic
(the path and other query parameters):

- `Next`: Redirect to the next new message's index.
- `Last`: Redirect to the newest buffered message, or use `Next` if the buffer is empty.
- A non-negative integer: Read that message, waiting if it has not arrived.

Add `LongPollingBatch` to receive several messages in one HTTP response:

- `?LongPollingIdx=42&LongPollingBatch=10`:
  Wait for messages 42 through 51, then return all ten.
- `?LongPollingIdx=42&LongPollingBatch=AllAvailable`:
  Return message 42 and all newer buffered messages. If 42 has not arrived, wait for it.
- `?LongPollingIdx=42` (no batch):
  Return only message 42, waiting if needed.

The server keeps the latest 100 messages per subscription. If the requested start is too old, a batch starts
at the oldest buffered message. A numeric batch still waits for exactly the requested count.
Without a batch, a too-old index redirects to the next message, as before.

Clients report detected gaps in `Message::error`, alongside the next valid sample.
The warning does not invalidate that sample or stop the subscription.

`LongPollingBatch` requires `LongPollingIdx`. The batch size must be 1–100 or `AllAvailable`;
it does not mean "the latest N messages" or a time range. Invalid values and index ranges too
large to represent return HTTP `400`.

Batches are intended for direct connections. Proxy caching is unchanged.

If a successful batch response cannot be decoded, the client reports an error and continues after its
last index, if known; otherwise it uses `Next`, which may skip buffered messages.
HTTP `504` retries the same index if known. Other HTTP errors stop the batch subscription.

Batch responses use `multipart/mixed`, with one part per message. Each part carries its own
index, topic, service name, and payload length. The outer `x-opencmw-long-polling-idx` header
gives the first returned index; topic and service name appear only in the parts.

Example with two messages. All line breaks are `\r\n`, including after the closing boundary:

```text
--opencmw-long-polling-multipart-boundary
x-opencmw-long-polling-idx: 42
x-opencmw-topic: /colors?sample=42
x-opencmw-service-name: colors-service
content-length: 5

hello
--opencmw-long-polling-multipart-boundary
x-opencmw-long-polling-idx: 43
x-opencmw-topic: /colors?sample=43
x-opencmw-service-name: colors-service
content-length: 5

world
--opencmw-long-polling-multipart-boundary--
```

Response header:
`content-type: multipart/mixed; boundary=opencmw-long-polling-multipart-boundary`.

The boundary is fixed. Read each payload by its `content-length`, not by searching for the
boundary. The length counts payload bytes only, excluding the following line break.

Generic MIME parsers can also be used, but they find parts by searching for boundary delimiter
lines rather than using `content-length`. We do not guarantee that the boundary is absent from
payloads. A matching delimiter line inside a payload may therefore be mistaken for the end of a part.
