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
By default, `PUT` and `POST` HTTP requests are mapped to Majordomo's `Post`,
and `GET` HTTP request is mapped to Majordomo's `Get` request.

It also defines two non-standard HTTP requests -- `SUB`,
which maps to Majordomo's `Subscribe`
and `POLL` which maps to `LongPoll`.

For clients that don't support a custom request operation,
the non-standard HTTP requests can be invoked by defining the
`x-opencmw-method` HTTP header.

If this header is defined, its value is used to override
the HTTP request method.
This means that if HTTP method is `GET` and `x-opencmw-method=POLL`,
that the REST backend will treat it as `LongPoll` request.

Alternatively, one can override the Majordomo method
by setting the `LongPollingIdx` query parameter.
The `Subscription` value of this parameter is mapped to Majordomo's `Subscription` method,
while an integer value will activate the `LongPoll` method.
