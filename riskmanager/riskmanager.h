#pragma once

#include "../include/models.h"
#include <unordered_set>

namespace oms {

class RiskManager {
public:
    RiskResult validateOrder(const OrderRequest& request, const Portfolio& portfolio);

private:
    bool isExchangeSupported(const std::string& exchange);
    bool isSymbolValid(const std::string& symbol, const std::string& exchange);
    bool isQuantityValid(double quantity, OrderType type);
    bool isPriceValid(double price, OrderType type);
    bool hasBuyingPower(const OrderRequest& request, const Portfolio& portfolio);
    bool withinPositionLimits(const OrderRequest& request, const Portfolio& portfolio);
    bool withinExposureLimits(const OrderRequest& request, const Portfolio& portfolio);
    bool withinDailyLoss(const Portfolio& portfolio);
    bool checkRequiredFields(const OrderRequest& request);

    std::unordered_set<std::string> supported_exchanges_ = {"EXCHANGE1", "EXCHANGE2", "EXCHANGE3"};
    std::unordered_set<std::string> valid_symbols_ = {"AAPL", "GOOGL", "MSFT", "AMZN", "TSLA", "META", "NVDA", "JPM", "BAC", "GS"};
    double max_position_per_symbol_ = 10000.0;
    double max_exposure_ = 500000.0;
    double max_daily_loss_ = 10000.0;
};

}
