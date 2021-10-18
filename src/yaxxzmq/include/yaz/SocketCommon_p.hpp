#ifndef YAZ_SOCKET_COMMON_H
#define YAZ_SOCKET_COMMON_H

#include "Meta.hpp"

namespace yaz {

// We want to support several types of handlers
// - values or pointers to handlers
// - handlers that have the call operator (for lambdas),
//   and handlers that have a handle_message member function
// - handlers that accept a socket plus a message, or just a
//   message
template<typename Sender, typename Handler, typename Message>
void pass_to_message_handler(Sender &sender, Handler &&handler, Message &&message) {
    auto *               handler_ptr = meta::to_pointer(YAZ_FWD(handler));

    constexpr const bool has_full_handle_message_v
            = requires {
        handler_ptr->handle_message(sender, YAZ_FWD(message));
    };
    constexpr const bool has_simple_handle_message_v
            = requires {
        handler_ptr->handle_message(YAZ_FWD(message));
    };

    constexpr const bool has_full_call_operator_v = requires {
        std::invoke(*handler_ptr, sender, YAZ_FWD(message));
    };
    constexpr const bool has_simple_call_operator_v
            = requires {
        std::invoke(*handler_ptr, YAZ_FWD(message));
    };

    if constexpr (has_full_handle_message_v) {
        handler_ptr->handle_message(sender, YAZ_FWD(message));
    }
    else if constexpr (has_simple_handle_message_v) {
        handler_ptr->handle_message(YAZ_FWD(message));
    }
    else if constexpr (has_full_call_operator_v) {
        std::invoke(*handler_ptr, sender, YAZ_FWD(message));
    }
    else if constexpr (has_simple_call_operator_v) {
        std::invoke(*handler_ptr, YAZ_FWD(message));
    }
    else {
        meta::error_print_types<decltype(*handler_ptr)>{};
        static_assert(meta::always_false<decltype(handler_ptr)>, "Handler does not have handle_message or operator()");
    }
}

} // namespace yaz

#endif // include guard
