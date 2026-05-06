#include "ledgerflow/core/event_mapper.hpp"
#include "ledgerflow/server.hpp"

#include <cstdint>
#include <variant>

int main() {
    IngressRequest request;
    request.set_type(REQUEST_TYPE_GET_POSITION);
    request.mutable_get_position_request()->set_symbol("AAPL");

    if (!request.has_get_position_request()) {
        return 1;
    }

    if (request.get_position_request().symbol() != "AAPL") {
        return 2;
    }

    IngressResponse response;
    auto* position = response.mutable_position_response()->mutable_position();
    position->set_sym(request.get_position_request().symbol());
    position->set_quantity(100);
    position->set_last_sequence_number(1);

    if (response.position_response().position().quantity() != 100) {
        return 3;
    }

    IngressRequest eventRequest;
    eventRequest.set_type(REQUEST_TYPE_EXECUTION_EVENT);
    auto* executionEvent = eventRequest.mutable_execution_event();
    executionEvent->set_symbol("AAPL");
    executionEvent->mutable_hdr()->set_ingest_ts_ns(42);
    executionEvent->mutable_hdr()->set_schema_version(1);
    executionEvent->mutable_hdr()->set_type(EVENT_TYPE_ORDER);
    executionEvent->set_order_id(7);
    executionEvent->set_side(SIDE_BUY);
    executionEvent->set_status(ORDER_STATUS_ACCEPTED);
    executionEvent->set_signed_order_qty(100);
    executionEvent->set_limit_price(12345);
    executionEvent->set_avg_entry_price(12300);

    const auto mappedEvent = ledgerflow::core::event_mapper::toInternalEvent(eventRequest);
    const auto* mappedOrder = std::get_if<ledgerflow::core::events::OrderEvent>(&mappedEvent);
    if (mappedOrder == nullptr) {
        return 4;
    }

    if (mappedOrder->symbol != "AAPL" || mappedOrder->order_id != 7 || mappedOrder->signed_order_qty != 100) {
        return 5;
    }

    return mappedOrder->side == ledgerflow::core::events::Side::Buy ? 0 : 6;
}
