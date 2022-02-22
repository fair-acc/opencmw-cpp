# opencmw client library

The client sub-library contains client code for various protocols (opencmw, cmwlight, rest, ...) and facilities to
publish responses and subscription updates into an event store.

## Architecture

``` 
___________________________________________________________________________________________________
|                     ___________________                 _____                                   |
|     == cmw-sub ==> | cmw-client        | ------->     / ring \     -----> consumers             |
|                    |ClientPublisher|             | buffer |               ^                 |
|     == rest-sub => | rest-client       |              \______/                |                 |
|                    |___________________|                                  setup consumers       |
|                             ^                                                 |                 |
|                             |                                                 |                 |
|                         control socket     <-- setup subscriptions      setup-thread            |
|_________________________________________________________________________________________________|
```

The `ClientPublisher` is compromised by one poll loop, which polls all active connections and a control socket, 
which is used to set up new requests. All replies are deserialised and published into a disruptor, along with some
metadata.

The control socket can be accessed by any thread to request new subscriptions and/or connections

## Clients
There are different clients available to use with the datasource publisher. If you want to implement your own event
loop you can also use them standalone.

### opencmw-client
Implements a client for the opencmw majordomo protocol.

### rest-client
Implements a HTTP/REST client based on httplib-client.

### cmwlight-client
Implements the client interface for the existing CMW based control system.

## TODO:
- one connection per subscription or service?
- also implement future based get API?