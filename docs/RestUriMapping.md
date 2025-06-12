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

Requests with a query parameter `LongPollingIdx` are treated as `LongPoll` request subscribing to the
given topic (consisting of URI path and other query parameters), with the possible values:

 - `Next`: Redirects to the next notification message that arrives after the request has been processed.
 - `Last`: Redirects to the most recent notification message that is in the cache when the request is
 processed. If there is no such entry yet, it's treated like `Next`, i.e. waits for the next notification.
 - a positive integer value, to retrieve a specific cache entry.
