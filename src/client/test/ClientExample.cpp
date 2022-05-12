#include <ClientContext.hpp>
#include <Client.hpp>

using namespace opencmw::client;
using opencmw::majordomo::Context;
using opencmw::uri_check::STRICT;
using opencmw::URI;

class ClientCtxBase {
public:
    virtual ~ClientCtxBase() = default;
    virtual ClientBase &getClient(const URI<STRICT> &uri);
    virtual std::vector<std::string> supportedProtocols();
};

class MDPClientCtx : ClientCtxBase {
    ~MDPClientCtx() override = default;
    ClientBase &getClient(const URI<STRICT> &uri) override {

    }
};

int main() {
    Context zctx(); // zeromq context to be used by the zeromq protocols
    URI uri{""}; // uri of example endpoint
    auto cb = [](RawMessage&&) {};
    ClientContext ctx({});
    ctx.get(uri, cb);
    ctx.get<CmwLightClient>(uri, cb, zctx);
}