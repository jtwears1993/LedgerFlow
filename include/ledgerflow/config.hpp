//
// Created by jtwears on 5/6/26.
//
#pragma once

#include "ledgerflow/wal/wal.hpp"
#include "server.hpp"

namespace ledgerflow {

    struct ApplicationConfig {
       ServerConfig server_config;
       wal::WalConfig wal_config;
    };

}
