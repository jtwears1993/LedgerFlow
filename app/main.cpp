//
// Created by jtwears on 4/12/26.
//


#include "ledgerflow/config.hpp"
#include "ledgerflow/shutdown_manager.hpp"
#include "ledgerflow/core/event_mapper.hpp"
#include "ledgerflow/wal/wal.hpp"

#include <chrono>
#include <iostream>
#include <stop_token>
#include <thread>
#include <vector>

int main(const int argc, char *argv[]) {

     auto [server_config, wal_config] = ledgerflow::ApplicationConfig();
     auto wal = ledgerflow::wal::WriteAheadLog(wal_config);
     auto pos_engine = ledgerflow::BasicPositionEngine(ledgerflow::core::enums::ProductType::SPOT, 100);
     auto shutdown_manager = ledgerflow::ShutdownManager();

     std::vector<ledgerflow::wal::WalRecord> wal_records;
     wal.recover(wal_records);

     if (!wal_records.empty()) {
          std::cout << "Recovered " << wal_records.size() << " records from WAL." << std::endl;
          auto event = ledgerflow::core::events::Event{};
          for (const auto&[header, data] : wal_records) {
               switch (auto type = static_cast<RequestType>(header.event_type)) {
                    case REQUEST_TYPE_EXECUTION_EVENT || REQUEST_TYPE_TOP_OF_BOOK: {
                         IngressRequest request;
                         request.ParseFromArray(data.data(), static_cast<int>(data.size()));
                         event = ledgerflow::core::event_mapper::toInternalEvent(request);
                         break;
                    }
                    default:
                         continue;
               }
               pos_engine.onEvent(event);
          }
     }

     auto server = ledgerflow::Server(server_config, wal, pos_engine);
     std::stop_source stop_source;
     shutdown_manager.add_shutdown_request([&stop_source]() {
          stop_source.request_stop();
     });


     std::thread server_thread([&server, &stop_source]() {
          server.start(stop_source.get_token());
     });

     while (!shutdown_manager.shutdown_requested()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
     }
     shutdown_manager.run_shutdown_request();
     server_thread.join();
     return 0;
};
